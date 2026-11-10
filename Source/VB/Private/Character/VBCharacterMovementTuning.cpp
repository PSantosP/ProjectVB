// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "VBCharacterMovementTuning.h"

#include "Animation/VBAnimInstance.h"
#include "Character/VBCharacter.h"
#include "Character/VBSlideMovementComponent.h"
#include "Character/VBWeaponStateComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBMovementConfig.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "VB/VB.h"

namespace VBCharacterMovementTuning
{
	void ApplyConfig(UCharacterMovementComponent& CMC, const UVBMovementConfig& Config)
	{
		const UVBMovementConfig* Cfg = &Config;
		CMC.MaxAcceleration = Cfg->MaxAcceleration;
		CMC.BrakingDecelerationWalking = Cfg->BrakingDecelerationWalking;
		CMC.GroundFriction = Cfg->GroundFriction;
		CMC.BrakingFrictionFactor = Cfg->BrakingFrictionFactor;
		CMC.JumpZVelocity = Cfg->JumpZVelocity;
		CMC.AirControl = Cfg->AirControl;
		CMC.RotationRate = Cfg->DefaultRotationRate; // tick 이 매 프레임 갱신(baseline 만 확정)
		if (UVBSlideMovementComponent* SlideCMC = Cast<UVBSlideMovementComponent>(&CMC))
		{
			SlideCMC->SetMovementConfig(Cfg); // 걷기, 앉은 속도는 이동 컴포넌트가 자기 틱에서 정한다
			SlideCMC->SlideStartSpeed = Cfg->SlideStartSpeed;
			SlideCMC->SlideEndSpeed = Cfg->SlideEndSpeed;
			SlideCMC->SlideMaxDuration = Cfg->SlideMaxDuration;
			SlideCMC->SlideSettings = Cfg->SlideSettings;
			SlideCMC->SlideMaxSpeed = Cfg->SlideMaxSpeed;
			// 이 설정이면 슬라이드가 시작하지 않는다(UVBSlideMovementComponent::CanRequestSlide). 조용히 안 되면 C 키가 안 먹는 것으로만 보인다.
			VB_CLOG(Cfg->SlideEndSpeed >= Cfg->SlideStartSpeed, Warning,
				"이동 설정 %s: SlideEndSpeed(%.0f) >= SlideStartSpeed(%.0f) - 슬라이드를 시작하지 않는다",
				*GetNameSafe(Cfg), Cfg->SlideEndSpeed, Cfg->SlideStartSpeed);
		}

		// 회전 칸 셋(루트모션 회전 포함)은 이동 컴포넌트가 이동 직전에 정한다(UVBSlideMovementComponent::RefreshRotationMode).
	}

	void RefreshRotationRate(const AVBCharacter& Body, UCharacterMovementComponent& CMC, const UVBWeaponStateComponent* WeaponState)
	{
		// 입력이 없으면 유한 회전율로 제자리 회전의 남은 각을 보존하고, 이동 중에는 캡슐을 목표 방향으로 바로 돌린다.
		const bool bArmedGrounded = !CMC.IsFalling()
			&& WeaponState
			&& WeaponState->IsInCombat();
		const FVector InputAcceleration = CMC.GetCurrentAcceleration();
		const UVBAnimInstance* VBAnim = Cast<UVBAnimInstance>(Body.GetMesh() ? Body.GetMesh()->GetAnimInstance() : nullptr);
		const UVBAnimInstance* AnimSettings = VBAnim;
		if (!AnimSettings && Body.GetMesh() && Body.GetMesh()->GetAnimClass())
		{
			// 서버/초기화 경계에서도 같은 AnimClass 설정을 사용한다. 런타임 포즈 상태는 CDO에서 읽지 않는다.
			AnimSettings = Cast<UVBAnimInstance>(Body.GetMesh()->GetAnimClass()->GetDefaultObject());
		}

		const bool bGASPMoving = CMC.IsMovingOnGround()
			&& !FVector(InputAcceleration.X, InputAcceleration.Y, 0.f).IsNearlyZero()
			&& AnimSettings && AnimSettings->bUseGASPLocomotion
			&& !(VBAnim && (VBAnim->IsAnyMontagePlaying() || VBAnim->ShouldReleaseGASPRootOffset()))
			&& !Body.IsPlayingRootMotion() && !CMC.HasRootMotionSources()
			&& !Body.IsCapsuleRotationOwnedByFeature();
		const UVBMovementConfig* Cfg = Body.GetMovementConfig();
		FRotator Target = CMC.IsFalling()
			? Cfg->AirborneRotationRate
			: (bArmedGrounded && !bGASPMoving ? Cfg->ArmedGroundedRotationRate : Cfg->DefaultRotationRate);

		// 같은 값이면 쓰지 않는다.
		if (!CMC.RotationRate.Equals(Target, 0.01f))
		{
			CMC.RotationRate = Target;
		}
	}
}
