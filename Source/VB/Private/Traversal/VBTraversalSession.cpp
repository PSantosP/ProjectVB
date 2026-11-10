// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Traversal/VBTraversalSession.h"

#include "Character/VBCharacter.h"
#include "Character/VBWeaponStateComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	// 서버 권한과 무장 가드는 TempSheatheForParkour 안에서 본다.
	void StowWeapon(const AVBCharacter& Character)
	{
		if (UVBWeaponStateComponent* Weapons = UVBWeaponStateComponent::FindFor(&Character))
		{
			Weapons->TempSheatheForParkour();
		}
	}
}

void FVBTraversalSession::IgnoreObstacle(AVBCharacter& Character, UPrimitiveComponent* Obstacle)
{
	if (!Obstacle)
	{
		return;
	}

	if (UCapsuleComponent* Capsule = Character.GetCapsuleComponent())
	{
		Capsule->IgnoreComponentWhenMoving(Obstacle, true);
		IgnoredObstacle = Obstacle;
	}
}

void FVBTraversalSession::EnterMove(AVBCharacter& Character, UPrimitiveComponent* Obstacle, EMovementMode RecoverMode)
{
	RecoverMovementMode = RecoverMode;
	StowWeapon(Character);
	// Flying 이어야 루트모션이 Z 로도 움직인다.
	if (UCharacterMovementComponent* Movement = Character.GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Flying);
	}

	IgnoreObstacle(Character, Obstacle);
}

void FVBTraversalSession::EnterSurface(AVBCharacter& Character, UPrimitiveComponent* Obstacle)
{
	StowWeapon(Character);
	// 캡슐 회전 플래그는 이동 컴포넌트가 다음 이동 전에 끈다.
	if (UCharacterMovementComponent* Movement = Character.GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Flying);
		Movement->StopMovementImmediately();
	}

	IgnoreObstacle(Character, Obstacle);
}

void FVBTraversalSession::Restore(AVBCharacter* Character)
{
	if (Character)
	{
		// 임시로 치웠던 경우에만 무기가 돌아온다.
		if (UVBWeaponStateComponent* Weapons = UVBWeaponStateComponent::FindFor(Character))
		{
			Weapons->RestoreFromTempSheathe();
		}

		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->SetMovementMode(RecoverMovementMode);
		}

		Character->SetAnimRootMotionTranslationScale(1.0f);
		if (IgnoredObstacle)
		{
			if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
			{
				Capsule->IgnoreComponentWhenMoving(IgnoredObstacle, false);
			}
		}
	}

	IgnoredObstacle = nullptr;
}
