// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/GameplayCues/VBGC_CombatFeedback.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Camera/VBCombatFeedbackModifier.h"
#include "Camera/VBFinisherDirectorSubsystem.h"
#include "Data/VBCombatFeedbackProfile.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TimeControl/VBTimeDilationSubsystem.h"
#include "VB/VB.h"

namespace
{
	// 전투 연출 슬로우의 요청 이름(패링, 가드, 강타 공용). 같은 이름 재요청은 처음부터 다시라 겹쳐 쌓이지 않는다.
	const FName GFeedbackSlowHandle(TEXT("CombatFeedback"));

	FRotator AlignRotation(EVBFeedbackEffectAlign Align, const FVector& Normal, const FVector& Direction)
	{
		const FVector Axis = Align == EVBFeedbackEffectAlign::EventDirection && !Direction.IsNearlyZero() ? Direction
			: (Align != EVBFeedbackEffectAlign::None ? Normal : FVector::ZeroVector);
		return Axis.IsNearlyZero() ? FRotator::ZeroRotator : Axis.Rotation();
	}

	UVBCombatFeedbackModifier* FindFeedbackModifier(APlayerCameraManager& Manager)
	{
		UCameraModifier* Modifier = Manager.FindCameraModifierByClass(UVBCombatFeedbackModifier::StaticClass());
		return Cast<UVBCombatFeedbackModifier>(Modifier ? Modifier : Manager.AddNewCameraModifier(UVBCombatFeedbackModifier::StaticClass()));
	}

	// 켜진 컴포넌트를 돌려준다. 화면 연출이 나이를 제어하는 컴포넌트는 재사용되면 안 돼 일회 auto-destroy, 나머지는 풀에서 가져온다.
	UNiagaraComponent* SpawnFeedbackEffect(UWorld* World, const FVBCombatFeedbackEffect& Layer, const FVBCombatFeedbackEvent& Event,
		bool bScreenDriven, const FString& LogName)
	{
		if (!Layer.System || !FMath::IsFinite(Layer.Scale) || Layer.Scale <= 0.0f)
		{
			return nullptr;
		}

		// 꺼진 채 스폰해 User 값을 넣고 켠다.
		UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Layer.System, Event.Location,
			AlignRotation(Layer.Align, Event.Normal, Event.Direction), FVector(Layer.Scale), true, false,
			bScreenDriven ? ENCPoolMethod::None : ENCPoolMethod::AutoRelease, /*bPreCullCheck=*/!bScreenDriven);
		if (!Effect)
		{
			// 풀에서 가져온 것은 화면 밖이면 일부러 안 만든다(bPreCullCheck). 그것은 실패가 아니다.
			if (bScreenDriven)
			{
				VB_LOG(Warning, "VBGC_CombatFeedback[%s]: %s 스폰 실패", *LogName, *GetNameSafe(Layer.System));
			}

			return nullptr;
		}

		for (const TPair<FName, float>& Param : Layer.FloatParameters)
		{
			Effect->SetVariableFloat(Param.Key, Param.Value);
		}

		for (const TPair<FName, int32>& Param : Layer.IntParameters)
		{
			Effect->SetVariableInt(Param.Key, Param.Value);
		}

		for (const TPair<FName, FLinearColor>& Param : Layer.ColorParameters)
		{
			Effect->SetVariableLinearColor(Param.Key, Param.Value);
		}

		Effect->Activate(true);
		return Effect;
	}

	void PushFeedbackSlowMotion(UWorld* World, const UVBCombatFeedbackProfile& Profile, const FString& LogName)
	{
		if (UVBTimeDilationSubsystem* TimeDilation = UVBTimeDilationSubsystem::Get(World))
		{
			TimeDilation->PushSequence(GFeedbackSlowHandle, Profile.SlowMotionSteps);
		}
		else
		{
			VB_LOG(Warning, "VBGC_CombatFeedback[%s]: 시간 조정 서브시스템 미할당", *LogName);
		}
	}
}
APawn* UVBGC_CombatFeedback::ResolveScreenRecipient(AActor* Target, const FGameplayCueParameters& Parameters,
	EVBFeedbackRecipient Recipient)
{
	if (!IsValid(Target))
	{
		return nullptr;
	}

	APawn* Pawn = Cast<APawn>(Recipient == EVBFeedbackRecipient::Instigator ? Parameters.GetInstigator() : Target);
	return IsValid(Pawn) && Pawn->GetWorld() == Target->GetWorld() ? Pawn : nullptr;
}

bool UVBGC_CombatFeedback::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	UWorld* World = IsValid(MyTarget) ? MyTarget->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}

	if (!Profile)
	{
		VB_LOG(Warning, "VBGC_CombatFeedback[%s]: Profile 미할당 - 연출 없음", *GameplayCueTag.ToString());
		return false;
	}

	// 실제 HitResult 가 있으면 원점 접점도 유효하다. 문맥이 없는 호출만 액터 위치로 대체한다.
	const FHitResult* Contact = Parameters.EffectContext.GetHitResult();
	FVBCombatFeedbackEvent Event;
	Event.Profile = Profile;
	Event.Recipient = ResolveScreenRecipient(MyTarget, Parameters, Profile->ScreenRecipient);
	if (!Event.Recipient && (Profile->bScreenImpact || !Profile->SlowMotionSteps.IsEmpty()))
	{
		VB_LOG(Warning, "VBGC_CombatFeedback[%s]: valid feedback recipient missing for policy %d",
			*GameplayCueTag.ToString(), static_cast<int32>(Profile->ScreenRecipient));
	}
	Event.Location = Contact ? FVector(Contact->ImpactPoint)
		: (Parameters.Location.IsNearlyZero() ? MyTarget->GetActorLocation() : FVector(Parameters.Location));
	Event.Normal = FVector(Parameters.Normal).GetSafeNormal();
	// 막은 사건의 방향 = 공격자 -> 막은 캐릭터 + 위. 수평만 주면 정면 공격의 화면 반동이 0 이 된다.
	if (const AActor* Attacker = Parameters.GetInstigator(); Attacker && Attacker != MyTarget)
	{
		const FVector Incoming = MyTarget->GetActorLocation() - Attacker->GetActorLocation();
		Event.Direction = (FVector(Incoming.X, Incoming.Y, 0.0f).GetSafeNormal() + FVector::UpVector).GetSafeNormal();
	}

	// 접점이 받는 캡슐 안이면 이펙트가 가려진다 - 공격자 쪽 캡슐 바깥, 막는 칼 높이로 민다.
	if (const ACharacter* Character = Cast<ACharacter>(MyTarget); Character && Character->GetCapsuleComponent() && ContactStandOff > 0.0f)
	{
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		const FVector Center = Capsule->GetComponentLocation();
		const float Radius = Capsule->GetScaledCapsuleRadius();
		const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		if (FVector::Dist2D(Event.Location, Center) < Radius + ContactStandOff)
		{
			const AActor* Attacker = Parameters.GetInstigator();
			FVector Toward = Attacker ? Attacker->GetActorLocation() - Center : Character->GetActorForwardVector();
			Toward.Z = 0.0f;
			Toward = Toward.GetSafeNormal();
			if (Toward.IsZero())
			{
				Toward = Character->GetActorForwardVector();
			}

			Event.Location = Center + Toward * (Radius + ContactStandOff);
			Event.Location.Z = Center.Z + HalfHeight * ContactHeightRatio;
			Event.Normal = Toward;
		}
	}
	// 화면이 거절돼도 월드 FX 는 났다 - 큐로서는 처리한 것이다.
	PlayFeedback(World, Event, GameplayCueTag.ToString());
	return true;
}

bool UVBGC_CombatFeedback::PlayFeedback(UWorld* World, const FVBCombatFeedbackEvent& Event, const FString& LogName)
{
	const UVBCombatFeedbackProfile* Profile = Event.Profile;
	if (!World || !Profile || World->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	const APawn* Pawn = Event.Recipient;
	APlayerController* PC = IsValid(Pawn) && Pawn->IsLocallyControlled() ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	const bool bLocal = PC && PC->IsLocalController() && PC->PlayerCameraManager;
	const UVBFinisherDirectorSubsystem* Director = World->GetSubsystem<UVBFinisherDirectorSubsystem>();
	const bool bFinisherView = bLocal && Director && Director->IsFeedbackViewTarget(PC, Pawn, PC->PlayerCameraManager->GetViewTarget());
	// 다른 사람의 사건은 월드 FX 만 보인다. 로컬 화면, 시간 신호는 수신자 폰으로 정한다.
	UVBCombatFeedbackModifier* Modifier = bLocal && Profile->bScreenImpact ? FindFeedbackModifier(*PC->PlayerCameraManager) : nullptr;

	TArray<UNiagaraComponent*> Effects;
	int32 Spawned = 0;
	if (Event.bWorldEffects)
	{
		for (const FVBCombatFeedbackEffect& Layer : Profile->WorldEffects)
		{
			UNiagaraComponent* Effect = SpawnFeedbackEffect(World, Layer, Event, Modifier != nullptr, LogName);
			if (!Effect)
			{
				continue;
			}

			++Spawned;
			if (Modifier)
			{
				Effects.Add(Effect);
			}
		}
	}

	// 화면이 거절되면 FX 는 게임 시계로 제 수명을 돈다. 사건의 정체 = 프로필.
	const bool bScreen = Modifier && Modifier->StartFeedback(Event.Recipient,
		UVBCombatFeedbackModifier::ScaleSettings(Profile->Screen, Event.ScreenScale), Event.Direction, Effects, Profile);
	// 슬로우는 화면과 같은 우선순위를 따른다(약한 사건이 강한 슬로우를 다시 걸지 않게). 화면 연출이 없으면 그대로 건다.
	// 처형 디렉터가 자기 슬로우를 소유하므로 접촉 강조가 두 번째 전역 시계를 더하지 않는다.
	if (bLocal && !bFinisherView && !Profile->SlowMotionSteps.IsEmpty() && (!Profile->bScreenImpact || bScreen))
	{
		PushFeedbackSlowMotion(World, *Profile, LogName);
	}
	VB_LOG(Log, "전투 연출[%s]: %s 위치 %s, 로컬 %d, 월드 FX %d, 슬로우 %d단계, 화면 %d(우선 %d, 재질 %s)",
		*LogName, *GetNameSafe(Profile), *Event.Location.ToCompactString(), bLocal, Spawned,
		bLocal && !bFinisherView ? Profile->SlowMotionSteps.Num() : 0, bScreen, Profile->Screen.Priority,
		*GetNameSafe(Profile->Screen.Material));
	return bScreen;
}
