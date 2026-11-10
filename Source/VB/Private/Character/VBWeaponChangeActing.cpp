// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/VBWeaponChangeActing.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/VBMontageDebugLibrary.h" // GetMontageSlotName - 상체/전신 슬롯 판정
#include "Character/Appearance/VBCharacterAppearanceComponent.h"
#include "Character/VBCharacter.h"
#include "Character/VBWeaponVisualComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBWeaponDefinition.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "VB/VB.h"

namespace
{
	// 소환 VFX - 모든 전환 공통 (의료마법 소환 판타지). 손 소켓 1회성 어태치, 자동 소멸.
	void SpawnWeaponSummonVFX(AVBCharacter& Character, const FVBWeaponSummonData& Data)
	{
		if (!Data.SummonVFX)
		{
			return;
		}

		// 보이는 손에 붙인다(겉모습이 있으면 그 주 메시, 없으면 본체). 몽타주, 전환 추적은 호출한 쪽에서 본체 그대로다.
		USceneComponent* VFXParent = Character.GetMesh();
		FName VFXSocket = Data.SummonVFXSocket;
		if (const UVBCharacterAppearanceComponent* Appearance = Character.FindComponentByClass<UVBCharacterAppearanceComponent>())
		{
			Appearance->ResolveAttachTarget(Data.SummonVFXSocket, VFXParent, VFXSocket);
		}

		UNiagaraFunctionLibrary::SpawnSystemAttached(
			Data.SummonVFX, VFXParent, VFXSocket,
			FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::SnapToTarget, /*bAutoDestroy=*/true);
	}

	// 전환 몽타주 선택 - 전투 스탠스 변형, 탐험 변형, 꺼내기 순서다. 수납이면 수납 몽타주다.
	UAnimMontage* PickWeaponChangeMontage(const FVBWeaponSummonData& Data, EVBWeaponType FromType, EVBWeaponType ToType,
		bool bCombatStance)
	{
		if (ToType == EVBWeaponType::None)
		{
			return Data.SheatheMontage; // 수납 (콘텐츠 부재 시 null = VFX만)
		}

		// 전투 스탠스 변형(ver_B) 우선, 없으면 탐험(ver_A) 폴백 - 페어 키는 From 무기
		UAnimMontage* Montage = nullptr;
		if (bCombatStance)
		{
			if (const TObjectPtr<UAnimMontage>* Found = Data.ChangeFromMontagesCombat.Find(FromType))
			{
				Montage = *Found;
			}
		}

		if (!Montage)
		{
			if (const TObjectPtr<UAnimMontage>* Found = Data.ChangeFromMontages.Find(FromType))
			{
				Montage = *Found;
			}
		}

		if (!Montage)
		{
			// 처음 소환, 또는 짝 몽타주가 없는 교체 - 들어오는 무기의 꺼내기로 대신한다.
			Montage = Data.SummonMontage; // 콘텐츠 부재 시 null = VFX만
			if (Montage && FromType != EVBWeaponType::None)
			{
				VB_LOG(Log, "무기 전환 %s -> %s: 짝 몽타주 없음, 꺼내기(%s)로 대신",
					*UEnum::GetValueAsString(FromType), *UEnum::GetValueAsString(ToType), *GetNameSafe(Montage));
			}
		}

		return Montage;
	}

	// 전신 몽타주만 막는다 - 이동 중이면 발이 미끄러지고 앉기 중이면 일어섰다 앉는다.
	bool BlocksFullBodyChangeActing(const AVBCharacter& Character, float MaxSpeed)
	{
		return Character.GetVelocity().Size2D() > MaxSpeed || Character.bIsCrouched;
	}
}

EVBWeaponType VBWeaponChangeActing::GetDataWeapon(const FVBWeaponChangeRequest& Request)
{
	// To=None(수납)이면 From 무기 데이터를 사용 - 수납 연기/VFX는 사라지는 무기 소속.
	return (Request.ToType != EVBWeaponType::None) ? Request.ToType : Request.FromType;
}

void VBWeaponChangeActing::Play(AVBCharacter& Character, const UVBWeaponDefinition* Definition, const FVBWeaponChangeRequest& Request,
	float MaxFullBodySpeed)
{
	UVBWeaponVisualComponent* WeaponVisual = Character.FindComponentByClass<UVBWeaponVisualComponent>();
	if (WeaponVisual)
	{
		WeaponVisual->BeginWeaponTransition(Request.ToType, nullptr, nullptr);
	}

	if (!Definition)
	{
		return; // 정의 없는 무기 - 연기 없음 (등록은 BP_VBCharacter 컴포넌트의 WeaponDefinitions)
	}

	const FVBWeaponSummonData& Data = Definition->GetSummonData();
	SpawnWeaponSummonVFX(Character, Data);

	// 몽타주 선택은 반드시 게이트보다 먼저다(상체/전신 판정에 몽타주의 baked 슬롯이 필요).
	UAnimMontage* Montage = PickWeaponChangeMontage(Data, Request.FromType, Request.ToType, Request.bCombatStance);
	if (!Montage)
	{
		return; // 페어 콘텐츠 부재 무기 - 위 VFX 로 마무리
	}

	// 슬롯 판정 - 슬롯 이름이 UpperBody 인 몽타주만 상체로 본다(슬롯 없는 몽타주가 상체로 새지 않게).
	const bool bUpperBody = UVBMontageDebugLibrary::GetMontageSlotName(Montage) == TEXT("UpperBody");
	if (!bUpperBody && BlocksFullBodyChangeActing(Character, MaxFullBodySpeed))
	{
		return;
	}

	const USkeletalMeshComponent* Mesh = Character.GetMesh();
	if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
	{
		// 슬롯은 에셋 baked(UpperBody or DefaultSlot). 둘 다 DefaultGroup 이라 공격/회피가 자연 인터럽트(dodge cancel 보존).
		const float PlayedLength = AnimInst->Montage_Play(Montage);
		if (WeaponVisual && PlayedLength > 0.0f)
		{
			WeaponVisual->BeginWeaponTransition(Request.ToType, AnimInst, Montage);
		}

		VB_LOG(Log, "WeaponChangeActing: montage=%s from=%d to=%d combat=%d upperBody=%d",
			*Montage->GetName(), (int32)Request.FromType, (int32)Request.ToType, Request.bCombatStance ? 1 : 0, bUpperBody ? 1 : 0);
	}
}
