#include "Enemy/VBEnemyAuthoring.h"
#include "CharacterCommon/VBDeathDissolveComponent.h"
#include "CharacterCommon/VBMeshRefresh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBDeathDissolveConfig.h"
#include "Data/VBEnemyConfig.h"
#include "GameFramework/Actor.h"
#include "VB/VB.h"

void VBEnemyAuthoring::PreviewDeathDissolve(AActor& Owner, const UVBEnemyConfig& Config, UVBDeathDissolveComponent* DissolveComponent)
{
	// 여기서 null 검사를 하지 않는다 - 설정 폴백 판단은 컴포넌트 한 곳에만 둔다.
	UVBDeathDissolveConfig* Dissolve = UVBDeathDissolveComponent::ResolveConfig(Config.DeathDissolve.Get());
	if (!Dissolve || Dissolve->Duration <= 0.0f)
	{
		VB_LOG(Warning, "사망 디졸브 시험: %s 에 쓸 설정이 없다 - 적 설정과 프로젝트 기본(Project Settings > Game > VB Death Dissolve)이 둘 다 비었다.",
			*Owner.GetName());
		return;
	}

	if (!DissolveComponent)
	{
		VB_LOG(Warning, "사망 디졸브 시험: %s 에 디졸브 컴포넌트가 없다.", *Owner.GetName());
		return;
	}

	// 남은 시간 = 디졸브 길이로 전달해 즉시 시작한다. 컴포넌트는 Destroy 하지 않아 몇 번이든 다시 볼 수 있다.
	// 앞선 시험을 먼저 지운다 - 이전 시작 시각이 섞이면 진행률이 튄다.
	DissolveComponent->ResetDissolve();
	DissolveComponent->ScheduleDissolve(Dissolve->Duration, Dissolve);
}

void VBEnemyAuthoring::ResetDeathDissolvePreview(UVBDeathDissolveComponent* DissolveComponent)
{
	if (DissolveComponent)
	{
		DissolveComponent->ResetDissolve();
	}
}

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#define LOCTEXT_NAMESPACE "VBEnemyBase"

bool VBEnemyAuthoring::ValidateAnimation(USkeletalMeshComponent* Mesh, const UVBEnemyConfig& Config, FDataValidationContext& Context)
{
	// 저장 때 검증은 에디터 설정을 따른다(자동 저장은 건너뜀). 놓쳐도 런타임 SetBasePolicy 가 안전한 값으로 내린다.
	const EVisibilityBasedAnimTickOption TickOption = Config.AnimTickOption;
	if (Mesh && !FVBMeshRefresh::IsBasePolicySafe(TickOption, Mesh->GetAnimClass()))
	{
		Context.AddError(FText::Format(LOCTEXT("VBEnemy_UnsafeAnimTick",
			"애니메이션 클래스 {0} 에 연결 층이 있는데 설정 {1} 의 AnimTickOption 이 OnlyTickMontagesAndRefreshBonesWhenPlayingMontages 입니다. "
			"화면 밖에서 몽타주를 도는 순간 UE 5.8 이 null 을 읽고 터집니다. 실행 때는 AlwaysTickPose 로 내려갑니다."),
			FText::FromString(GetNameSafe(Mesh->GetAnimClass())), FText::FromString(GetNameSafe(&Config))));
		return false;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
#endif
