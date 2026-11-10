#include "Misc/AutomationTest.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstance.h"
#include "CharacterCommon/VBMeshRefresh.h"
#include "Engine/Blueprint.h"
#include "Misc/DataValidation.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBEnemyConfig.h"
#include "Enemy/VBEnemyBase.h"
#include "Enemy/VBEnemySetup.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// 적 메시의 평소 애니메이션 정책 - 적이 많을 때 게임 스레드 비용을 줄인다.
//   설정 기본값은 AlwaysTickPose(몽타주만 두 값은 연결 층이 있으면 5.8 에서 터진다) + URO 이고, 근접 판정 창이 쥔 동안에는
//   전부 갱신이 이기며, 창이 끝나면 설정 정책으로 돌아가야 한다.
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyAnimTickPolicyTest, "VB.Unit.Enemy.AnimTickPolicy",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBEnemyAnimTickPolicyTest::RunTest(const FString& Parameters)
{
	const UVBEnemyConfig* Defaults = GetDefault<UVBEnemyConfig>();
	TestEqual(TEXT("config default: AlwaysTickPose (montage-only options crash with linked layers)"), Defaults->AnimTickOption,
		EVisibilityBasedAnimTickOption::AlwaysTickPose);
	TestTrue(TEXT("config default: update rate optimizations on"), Defaults->bAnimUpdateRateOptimizations);

	const TStrongObjectPtr<USkeletalMeshComponent> Mesh(NewObject<USkeletalMeshComponent>(GetTransientPackage()));
	// ACharacter 생성자의 메시 기본값(Character.cpp) - 정책을 세우기 전 상태.
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
	Mesh->bEnableUpdateRateOptimizations = false;
	const auto IsForced = [&]() {
		return Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones
			&& !Mesh->bEnableUpdateRateOptimizations;
	};
	const auto IsConfigPolicy = [&]() {
		return Mesh->VisibilityBasedAnimTickOption == Defaults->AnimTickOption
			&& Mesh->bEnableUpdateRateOptimizations == Defaults->bAnimUpdateRateOptimizations;
	};

	VBEnemySetup::ApplyAnimTickPolicy(Mesh.Get(), *Defaults);
	TestTrue(TEXT("apply sets the config policy on an idle mesh"), IsConfigPolicy());

	// 반대 경우: 근접 판정 창(FVBMeshRefresh 를 쥔다)이 열리면 전부 갱신, 닫히면 설정 정책으로 돌아간다.
	FVBMeshRefresh::Acquire(Mesh.Get());
	TestTrue(TEXT("hit window forces full-rate bones over the config policy"), IsForced());
	FVBMeshRefresh::Release(Mesh.Get());
	TestTrue(TEXT("hit window end restores the config policy"), IsConfigPolicy());

	// 창이 쥔 동안 정책을 세워도 창을 깨지 않고, 창이 끝나면 새 정책으로 돌아간다(옛 저장값이 아니라).
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
	Mesh->bEnableUpdateRateOptimizations = false;
	FVBMeshRefresh::Acquire(Mesh.Get());
	VBEnemySetup::ApplyAnimTickPolicy(Mesh.Get(), *Defaults);
	TestTrue(TEXT("apply during a hit window keeps the window forced"), IsForced());
	FVBMeshRefresh::Release(Mesh.Get());
	TestTrue(TEXT("window end after apply restores the new policy, not the old saved value"), IsConfigPolicy());
	TestEqual(TEXT("no requests left"), FVBMeshRefresh::GetRequestCount(Mesh.Get()), 0);

	VBEnemySetup::ApplyAnimTickPolicy(nullptr, *Defaults); // 메시 없음: 할 일 없음, 터지지 않는다.
	return true;
}

// AnimTickGuard: 연결 층이 있는 애니메이션 클래스에
//   OnlyTickMontagesAndRefreshBonesWhenPlayingMontages 를 거절한다. 실제 자산으로 잰다 - 판정이 보는 것은 생성 클래스의 노드 목록이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyAnimTickGuardTest, "VB.Unit.Enemy.AnimTickGuard",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBEnemyAnimTickGuardTest::RunTest(const FString& Parameters)
{
	constexpr EVisibilityBasedAnimTickOption Unsafe = EVisibilityBasedAnimTickOption::OnlyTickMontagesAndRefreshBonesWhenPlayingMontages;
	UClass* EnemyAnim = LoadClass<UAnimInstance>(nullptr, TEXT("/Game/VB/Blueprints/Enemy/ABP_Enemy_Base.ABP_Enemy_Base_C"));
	if (!TestNotNull(TEXT("ABP_Enemy_Base loads"), EnemyAnim))
	{
		return false;
	}

	// 어떤 종류의 연결 노드가 실제 자산에 있는지 기록한다(그래프만, 층만, 상속을 따로 덮을 자산이 있는지 - 설계 2판 F1).
	const TCHAR* MeasuredPaths[] = {
		TEXT("/Game/VB/Blueprints/Enemy/ABP_Enemy_Base.ABP_Enemy_Base_C"),
		TEXT("/Game/VB/Blueprints/Enemy/ABP_Boss_Troll.ABP_Boss_Troll_C"),
		TEXT("/Game/VB/Animations/LookAtPOI/ABP_VBEnemyLookAtPOI.ABP_VBEnemyLookAtPOI_C"),
	};
	for (const TCHAR* Path : MeasuredPaths)
	{
		UClass* Class = LoadClass<UAnimInstance>(nullptr, Path);
		const IAnimClassInterface* Interface = IAnimClassInterface::GetFromClass(Class);
		AddInfo(FString::Printf(TEXT("%s: graph nodes %d, layer nodes %d, super %s"), Path,
			Interface ? Interface->GetLinkedAnimGraphNodeProperties().Num() : -1,
			Interface ? Interface->GetLinkedAnimLayerNodeProperties().Num() : -1,
			Class ? *GetNameSafe(Class->GetSuperClass()) : TEXT("-")));
	}

	TestFalse(TEXT("ABP_Enemy_Base + montage-only-refresh is refused"), FVBMeshRefresh::IsBasePolicySafe(Unsafe, EnemyAnim));
	TestTrue(TEXT("ABP_Enemy_Base + AlwaysTickPose is allowed"),
		FVBMeshRefresh::IsBasePolicySafe(EVisibilityBasedAnimTickOption::AlwaysTickPose, EnemyAnim));
	TestTrue(TEXT("ABP_Enemy_Base + OnlyTickMontagesWhenNotRendered is allowed (not on this crash path)"),
		FVBMeshRefresh::IsBasePolicySafe(EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered, EnemyAnim));
	// 반대 경우: 애니메이션 BP 가 아닌 클래스에는 연결 노드가 없다.
	TestTrue(TEXT("native UAnimInstance + montage-only-refresh is allowed"),
		FVBMeshRefresh::IsBasePolicySafe(Unsafe, UAnimInstance::StaticClass()));
	TestTrue(TEXT("null class + montage-only-refresh is allowed"), FVBMeshRefresh::IsBasePolicySafe(Unsafe, nullptr));

	// 런타임: 메시에 들어가는 값. 거절은 에러 한 줄씩 - 두 번(바로 쓰기, 창이 쥔 동안의 저장값).
	AddExpectedMessagePlain(TEXT("ABP_Enemy_Base_C"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 2);
	const TStrongObjectPtr<USkeletalMeshComponent> Mesh(NewObject<USkeletalMeshComponent>(GetTransientPackage()));
	Mesh->AnimClass = EnemyAnim;
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	FVBMeshRefresh::SetBasePolicy(Mesh.Get(), Unsafe, true);
	TestEqual(TEXT("idle mesh: refused value becomes AlwaysTickPose"), Mesh->VisibilityBasedAnimTickOption,
		EVisibilityBasedAnimTickOption::AlwaysTickPose);

	FVBMeshRefresh::Acquire(Mesh.Get());
	FVBMeshRefresh::SetBasePolicy(Mesh.Get(), Unsafe, true);
	FVBMeshRefresh::Release(Mesh.Get());
	TestEqual(TEXT("held mesh: last release restores AlwaysTickPose, not the refused value"), Mesh->VisibilityBasedAnimTickOption,
		EVisibilityBasedAnimTickOption::AlwaysTickPose);

	Mesh->AnimClass = UAnimInstance::StaticClass();
	FVBMeshRefresh::SetBasePolicy(Mesh.Get(), Unsafe, true);
	TestEqual(TEXT("class without linked nodes keeps the authored value"), Mesh->VisibilityBasedAnimTickOption, Unsafe);
	TestEqual(TEXT("no requests left"), FVBMeshRefresh::GetRequestCount(Mesh.Get()), 0);

#if WITH_EDITOR
	// 에디터 검증 진입(설계 2판 F1): 실제 BP 는 엔진 길(UBlueprint::IsDataValid -> 생성 클래스 CDO)로, 위험한 짝은 같은 생성 클래스의
	//   임시 인스턴스 + 임시 설정으로 본다. 자산은 바꾸지도 저장하지도 않는다.
	const FString GuardText = TEXT("OnlyTickMontagesAndRefreshBonesWhenPlayingMontages");
	const auto HasGuardError = [&GuardText](const FDataValidationContext& Context) {
		return Context.GetIssues().ContainsByPredicate([&GuardText](const FDataValidationContext::FIssue& Issue) {
			return Issue.Severity == EMessageSeverity::Error && Issue.Message.ToString().Contains(GuardText);
		});
	};
	UBlueprint* SoldierBlueprint = LoadObject<UBlueprint>(nullptr, TEXT("/Game/VB/Blueprints/Enemy/BP_EnemySoldier.BP_EnemySoldier"));
	if (!TestNotNull(TEXT("BP_EnemySoldier loads"), SoldierBlueprint)
		|| !TestNotNull(TEXT("BP_EnemySoldier has a generated class"), SoldierBlueprint->GeneratedClass.Get()))
	{
		return false;
	}

	FDataValidationContext AssetContext;
	SoldierBlueprint->IsDataValid(AssetContext);
	TestFalse(TEXT("BP_EnemySoldier as authored: no guard error"), HasGuardError(AssetContext));

	const TStrongObjectPtr<AVBEnemyBase> Enemy(NewObject<AVBEnemyBase>(GetTransientPackage(), SoldierBlueprint->GeneratedClass));
	TestEqual(TEXT("BP instance mesh carries the Blueprint's AnimClass"), Enemy->GetMesh()->GetAnimClass(), EnemyAnim);
	const TStrongObjectPtr<UVBEnemyConfig> TempConfig(NewObject<UVBEnemyConfig>(GetTransientPackage()));
	FObjectProperty* ConfigProperty = FindFProperty<FObjectProperty>(AVBEnemyBase::StaticClass(), TEXT("EnemyConfig"));
	if (!TestNotNull(TEXT("EnemyConfig property found"), ConfigProperty))
	{
		return false;
	}

	ConfigProperty->SetObjectPropertyValue_InContainer(Enemy.Get(), TempConfig.Get());

	// 결과값(Invalid)만 보지 않는다 - 다른 검증이 Invalid 를 내도 통과해 버린다. 안전한 짝과 견줘 에러가 정확히 하나 늘었는지,
	//   그 하나가 가드 문구인지를 본다. 안전한 짝의 다른 문제는 기록만 한다.
	TempConfig->AnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
	FDataValidationContext SafeContext;
	Enemy->IsDataValid(SafeContext);
	TestFalse(TEXT("safe pair: no guard error"), HasGuardError(SafeContext));
	for (const FDataValidationContext::FIssue& Issue : SafeContext.GetIssues())
	{
		AddInfo(FString::Printf(TEXT("temp instance issue without the guard (severity %d): %s"),
			static_cast<int32>(Issue.Severity), *Issue.Message.ToString()));
	}

	TempConfig->AnimTickOption = Unsafe;
	FDataValidationContext UnsafeContext;
	TestEqual(TEXT("unsafe pair: Invalid"), Enemy->IsDataValid(UnsafeContext), EDataValidationResult::Invalid);
	TestEqual(TEXT("unsafe pair: exactly one more error than the safe pair"), UnsafeContext.GetNumErrors(), SafeContext.GetNumErrors() + 1);
	TestTrue(TEXT("unsafe pair: guard error present"), HasGuardError(UnsafeContext));
#endif
	return true;
}
#endif
