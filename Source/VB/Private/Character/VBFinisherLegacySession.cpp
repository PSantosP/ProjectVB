// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/VBFinisherLegacySession.h"

#include "Components/ActorComponent.h"
#include "ContextualAnimSceneActorComponent.h"
#include "ContextualAnimSceneAsset.h"
#include "ContextualAnimTypes.h"      // FContextualAnimSceneBindingContext / FContextualAnimSceneBindings
#include "ContextualAnimUtilities.h"  // BP_CreateContextualAnimSceneBindingsForTwoActors
#include "Data/VBFinisherConfig.h"
#include "GameFramework/Actor.h"
#include "VB/VB.h"        // VB_LOG

UContextualAnimSceneActorComponent* FVBFinisherLegacySession::FindSceneComponent(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}

	// 폰 타입에 묶이지 않게 FindComponentByClass 로 찾는다.
	return const_cast<AActor*>(Actor)->FindComponentByClass<UContextualAnimSceneActorComponent>();
}

bool FVBFinisherLegacySession::IsActorInScene(const AActor* Actor)
{
	// 플러그인 상태를 진실원으로 쓴다 - 따로 든 플래그는 어긋날 수 있다.
	const UContextualAnimSceneActorComponent* Scene = FindSceneComponent(Actor);
	return Scene && Scene->IsInActiveScene();
}

AActor* FVBFinisherLegacySession::GetParticipant(const AActor* Owner, FName Role)
{
	const UContextualAnimSceneActorComponent* Scene = FindSceneComponent(Owner);
	const FContextualAnimSceneBinding* Binding = Scene && Scene->IsInActiveScene()
		? Scene->GetBindings().FindBindingByRole(Role) : nullptr;
	return Binding ? Binding->GetActor() : nullptr;
}

void FVBFinisherLegacySession::AppendParticipants(const AActor* Owner, TArray<AActor*>& OutActors)
{
	const UContextualAnimSceneActorComponent* Scene = FindSceneComponent(Owner);
	if (Scene && Scene->IsInActiveScene())
	{
		for (const FContextualAnimSceneBinding& Binding : Scene->GetBindings())
		{
			if (AActor* Actor = Binding.GetActor())
			{
				OutActors.Add(Actor);
			}
		}
	}
}

float FVBFinisherLegacySession::GetPlaybackTime(const AActor* Owner)
{
	const UContextualAnimSceneActorComponent* Scene = FindSceneComponent(Owner);
	const FContextualAnimSceneBinding* Binding = Scene && Scene->IsInActiveScene()
		? Scene->GetBindings().FindBindingByActor(Owner) : nullptr;
	return Binding ? Binding->GetAnimMontageTime() : -1.0f;
}

bool FVBFinisherLegacySession::TransitionSection(const AActor* Owner, FName SectionName)
{
	UContextualAnimSceneActorComponent* Scene = FindSceneComponent(Owner);
	return Scene && Scene->TransitionContextualAnimScene(SectionName);
}

void FVBFinisherLegacySession::EarlyOut(const AActor* Owner)
{
	if (UContextualAnimSceneActorComponent* Scene = FindSceneComponent(Owner))
	{
		// 둘 다 멈춘다.
		Scene->EarlyOutContextualAnimScene(true);
	}
}

UContextualAnimSceneActorComponent* FVBFinisherLegacySession::Start(const UVBFinisherConfig& Config, AActor& Owner, AActor& Target,
	UActorComponent& TickOwner)
{
	UContextualAnimSceneActorComponent* OwnerScene = FindSceneComponent(&Owner);
	if (!OwnerScene || !FindSceneComponent(&Target))
	{
		// 기초 클래스가 보장하는 컴포넌트가 없다 - 그 액터 클래스의 설정 오류다.
		VB_LOG(Warning, "StartFinisher: SceneActorComponent 없음 (owner=%s target=%s). "
			"처형에 참여하는 액터는 AVBCharacter 또는 AVBEnemyBase 를 상속해야 한다",
			*GetNameSafe(&Owner), *GetNameSafe(&Target));
		return nullptr;
	}

	// 역할 배정은 씬 자산이 가진다. 여기서는 주역만 전달한다.
	const FContextualAnimSceneBindingContext Primary(&Owner);
	const FContextualAnimSceneBindingContext Secondary(&Target);

	FContextualAnimSceneBindings Bindings;
	if (!UContextualAnimUtilities::BP_CreateContextualAnimSceneBindingsForTwoActors(
		Config.Scene, Primary, Secondary, Bindings))
	{
		// 씬 자산의 진입 조건(SelectionCriteria)에 안 맞으면 여기서 실패한다. 정상 경로다.
		VB_LOG(Log, "StartFinisher: 바인딩 생성 실패 - 씬 조건 불충족 (scene=%s)", *GetNameSafe(Config.Scene));
		return nullptr;
	}

	AlignVictimToScene(Config, Bindings, Owner, Target, TickOwner);

	// 적은 몇 프레임에 걸쳐 오므로 바인딩을 여기서 다시 만들지 않는다.

	if (!OwnerScene->StartContextualAnimScene(Bindings))
	{
		VB_LOG(Warning, "StartFinisher: StartContextualAnimScene 실패 (scene=%s)", *GetNameSafe(Config.Scene));
		return nullptr;
	}

	return OwnerScene;
}

void FVBFinisherLegacySession::AlignVictimToScene(const UVBFinisherConfig& Config, const FContextualAnimSceneBindings& Bindings,
	AActor& Owner, AActor& Target, UActorComponent& TickOwner)
{
	// 클립이 기대하는 자리로 적을 데려온다 - 플레이어는 끌려가지 않는다.
	// 적에는 모션워핑이 없어 AlignBlendTime 동안 나눠 민다. 목표 자리는 씬 자산에서 읽는다.
	const FContextualAnimSceneBinding* OwnerBinding = Bindings.FindBindingByActor(&Owner);
	const FContextualAnimSceneBinding* TargetBinding = Bindings.FindBindingByActor(&Target);
	if (!OwnerBinding || !TargetBinding)
	{
		return;
	}

	const FName OwnerRole = Bindings.GetRoleFromBinding(*OwnerBinding);
	const FName TargetRole = Bindings.GetRoleFromBinding(*TargetBinding);

	// 첫 구간, 첫 애니 세트의 시작 시점에 피해자가 공격자에 대해 서야 할 자리다.
	const FTransform RelToOwner = Config.Scene->GetAlignmentTransformForRoleRelativeToOtherRole(0, 0, TargetRole, OwnerRole, 0.0f);

	// 역할 이름을 못 찾으면 플러그인이 조용히 항등을 돌려줘 두 캐릭터가 겹친다.
	// 자리 맞춤만 건너뛰고 장면은 진행한다.
	if (RelToOwner.Equals(FTransform::Identity))
	{
		VB_LOG(Warning, "StartFinisher: 정렬 변환이 항등이다 - 역할 이름 불일치로 보인다 "
			"(scene=%s roles=%s/%s). 자리 맞춤을 건너뛴다",
			*GetNameSafe(Config.Scene), *OwnerRole.ToString(), *TargetRole.ToString());
		return;
	}

	BeginAlign(Target, RelToOwner * Owner.GetActorTransform(), Config.AlignBlendTime, TickOwner);
}

void FVBFinisherLegacySession::BeginAlign(AActor& Victim, const FTransform& Desired, float BlendTime, UActorComponent& TickOwner)
{
	// 어긋남은 지금 한 번만 잰다 - 매 틱 재면 루트모션 이동까지 어긋남으로 읽는다.
	const FVector StartLoc = Victim.GetActorLocation();
	const float StartYaw = Victim.GetActorRotation().Yaw;

	AlignActor = &Victim;
	AlignOffset = Desired.GetLocation() - StartLoc;

	// 회전은 yaw 만, 최단 경로로(350도 = -10도).
	AlignYaw = FRotator::NormalizeAxis(Desired.Rotator().Yaw - StartYaw);
	AlignTime = FMath::Max(BlendTime, 0.0f);

	if (AlignTime <= KINDA_SMALL_NUMBER)
	{
		// 0 이면 그 자리에서 다 민다(즉시 이동).
		Victim.AddActorWorldOffset(AlignOffset, false, nullptr, ETeleportType::TeleportPhysics);
		Victim.AddActorWorldRotation(FRotator(0.0f, AlignYaw, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
		AlignActor = nullptr;
		VB_LOG(Log, "StartFinisher: 적을 즉시 데려왔다 (%.0fcm, %.0f도)",
			AlignOffset.Size(), AlignYaw);
		return;
	}

	TickOwner.SetComponentTickEnabled(true);

	// 틱이 안 켜지면 보정이 통째로 빠지므로 확인하고 경고한다.
	if (!TickOwner.IsComponentTickEnabled())
	{
		VB_LOG(Warning, "StartFinisher: 틱을 못 켰다 - 적을 그 자리에서 옮긴다 "
			"(PrimaryComponentTick.bCanEverTick 을 확인해라)");
		Victim.AddActorWorldOffset(AlignOffset, false, nullptr, ETeleportType::None);
		Victim.AddActorWorldRotation(FRotator(0.0f, AlignYaw, 0.0f), false, nullptr, ETeleportType::None);
		AlignActor = nullptr;
		return;
	}

	VB_LOG(Log, "StartFinisher: 적을 %.2f초에 걸쳐 데려온다 (%.0fcm, %.0f도)",
		AlignTime, AlignOffset.Size(), AlignYaw);
}

void FVBFinisherLegacySession::TickAlign(float DeltaTime, UActorComponent& TickOwner)
{
	AActor* Victim = AlignActor.Get();
	if (!Victim || AlignTime <= 0.0f)
	{
		AlignActor = nullptr;
		TickOwner.SetComponentTickEnabled(false); // 할 일이 없는 틱은 켜 두지 않는다
		return;
	}

	// 이번 프레임 몫만 더한다 - 절대 위치를 세팅하면 루트모션 이동을 지운다.
	const float Step = FMath::Clamp(DeltaTime / AlignTime, 0.0f, 1.0f);

	const FVector MoveNow = AlignOffset * Step;
	const float TurnNow = AlignYaw * Step;

	// 나눠 미는 구간은 일반 이동이다 - 텔레포트로 밀면 원격 클라 보간이 끊긴다. 즉시 이동만 텔레포트.
	Victim->AddActorWorldOffset(MoveNow, false, nullptr, ETeleportType::None);
	Victim->AddActorWorldRotation(FRotator(0.0f, TurnNow, 0.0f), false, nullptr, ETeleportType::None);

	AlignOffset -= MoveNow;
	AlignYaw -= TurnNow;
	AlignTime -= DeltaTime;

	if (AlignTime <= 0.0f)
	{
		// 남은 찌꺼기를 마지막에 털어 넣는다. 안 털면 매번 조금씩 어긋난 채로 끝난다.
		Victim->AddActorWorldOffset(AlignOffset, false, nullptr, ETeleportType::None);
		Victim->AddActorWorldRotation(FRotator(0.0f, AlignYaw, 0.0f), false, nullptr, ETeleportType::None);
		AlignActor = nullptr;
		AlignOffset = FVector::ZeroVector;
		AlignYaw = 0.0f;
		TickOwner.SetComponentTickEnabled(false);
	}
}
