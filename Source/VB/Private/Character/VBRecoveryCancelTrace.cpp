// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/VBRecoveryCancelTrace.h"

#include "Animation/VBAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "PoseSearch/PoseSearchDatabase.h" // 진단 로그가 선택된 MM 풀 이름을 찍는다 - GetName() 에 완전 타입 필요
#include "TimerManager.h"
#include "VB/VB.h"

// 회복 캔슬 직후 이동 상태 추적(진단). 1 = 캔슬부터 0.05초 간격 8표본을 [ComboTrace] 로 찍는다.
static TAutoConsoleVariable<int32> CVarRecoveryCancelTrace(
	TEXT("vb.Combat.RecoveryCancelTrace"),
	0,
	TEXT("Combo recovery-cancel trace: 0=off, 1=log [ComboTrace] speed/root-motion for 0.4s after each cancel."),
	ECVF_Default
);

void FVBRecoveryCancelTrace::Begin(ACharacter& Body)
{
	if (CVarRecoveryCancelTrace.GetValueOnGameThread() == 0)
	{
		return;
	}

	UWorld* World = Body.GetWorld();
	if (!World)
	{
		return;
	}

	SamplesLeft = 8;
	World->GetTimerManager().ClearTimer(Timer);
	ACharacter* Owner = &Body;
	World->GetTimerManager().SetTimer(Timer, FTimerDelegate::CreateWeakLambda(Owner, [this, Owner]()
	{
		const UCharacterMovementComponent* CMC = Owner->GetCharacterMovement();
		if (!CMC)
		{
			return;
		}
		// 고른 풀도 찍는다 - 속도 0 인데 정지 클립이 나오면 원인은 궤적 이력이다.
		FString Pool = TEXT("(없음)");
		if (const UVBAnimInstance* VBAnim = Cast<UVBAnimInstance>(Owner->GetMesh() ? Owner->GetMesh()->GetAnimInstance() : nullptr))
		{
			if (const UPoseSearchDatabase* Db = VBAnim->GetCurrentSelectedDatabase())
			{
				Pool = Db->GetName();
			}
		}

		VB_LOG(Log, "[ComboTrace] 남은표본 %d | Speed2D %.0f | 루트모션 %s | 가속 %.0f | MM풀 %s",
			SamplesLeft, CMC->Velocity.Size2D(),
			Owner->IsPlayingRootMotion() ? TEXT("있음") : TEXT("없음"),
			CMC->GetCurrentAcceleration().Size2D(), *Pool);
		if (--SamplesLeft <= 0)
		{
			if (UWorld* W = Owner->GetWorld())
			{
				W->GetTimerManager().ClearTimer(Timer);
			}
		}
	}), 0.05f, true, 0.0f);
}
