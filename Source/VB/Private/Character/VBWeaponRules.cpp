// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/VBWeaponRules.h"

#include "Character/VBWeaponStateComponent.h"
#include "GameFramework/PlayerState.h"
#include "VB/VB.h"

namespace
{
	UVBWeaponStateComponent* GetInstigatorWeapons(const FVBRuleContext& Context)
	{
		return Context.Instigator ? UVBWeaponStateComponent::FindFor(Context.Instigator->GetPawn()) : nullptr;
	}
}

bool FVBCondition_HasWeapon::Evaluate(const FVBRuleContext& Context) const
{
	const UVBWeaponStateComponent* Weapons = GetInstigatorWeapons(Context);
	if (!Weapons)
	{
		// 읽을 수 없으면 거짓이다 - bNegate 여도 참으로 뒤집지 않는다(모르는 것을 '안 가졌다' 로 읽으면 빈손 대사가 엉뚱하게 나온다).
		VB_LOG(Warning, "조건(무기 가짐): 부른 플레이어의 무기 상태를 읽을 수 없다 - 거짓으로 친다");
		return false;
	}

	const bool bHas = (Weapon == EVBWeaponType::None) ? !Weapons->GetOwnedWeapons().IsEmpty() : Weapons->IsWeaponOwned(Weapon);
	return bNegate ? !bHas : bHas;
}

void FVBEffect_GrantWeapon::Apply(const FVBRuleContext& Context) const
{
	UVBWeaponStateComponent* Weapons = GetInstigatorWeapons(Context);
	if (!Weapons)
	{
		VB_LOG(Warning, "효과(무기 주기): 부른 플레이어의 무기 상태가 없다 - 주지 못함, 이어지는 효과 %d개를 건너뛴다", OnGrantedEffects.Num());
		Context.ReportEffectFailure();
		return;
	}

	if (Weapons->GrantWeapon(Weapon) == EVBWeaponGrantResult::Failed)
	{
		VB_LOG(Warning, "효과(무기 주기): %s 를 주지 못함 - 이어지는 효과 %d개를 건너뛴다", *UEnum::GetValueAsString(Weapon), OnGrantedEffects.Num());
		Context.ReportEffectFailure();
		return;
	}

	VBRules::ApplyAll(OnGrantedEffects, Context);
}

void FVBEffect_RevokeWeapon::Apply(const FVBRuleContext& Context) const
{
	if (Weapon == EVBWeaponType::None)
	{
		VB_LOG(Error, "효과(무기 거두기): 거둘 무기가 None 이다 - 데이터를 고친다");
		Context.ReportEffectFailure();
		return;
	}

	UVBWeaponStateComponent* Weapons = GetInstigatorWeapons(Context);
	if (!Weapons)
	{
		VB_LOG(Warning, "효과(무기 거두기): 부른 플레이어의 무기 상태가 없다 - 거두지 못함");
		return;
	}

	Weapons->RevokeWeapon(Weapon);
}
