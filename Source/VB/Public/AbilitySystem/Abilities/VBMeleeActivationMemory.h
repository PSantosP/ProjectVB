// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/VBWeaponStateTypes.h" // EVBWeaponType
#include "Data/VBComboSelectionPolicy.h" // FVBComboStartMemory - 띠마다 지난 시작 콤보를 값으로 든다

class UAnimMontage;
class UVBAttackConfig;

/**
 * 근접 공격 능력이 활성화 사이에 유지하는 상태: 변형 순환 커서, 거리 구간별 지난 시작 콤보, 회피가 끊은 콤보의 재개.
 * 능력 인스턴스가 유지되는 동안 남는다(Pawn 이 바뀌어도 지우지 않는다).
 */
struct VB_API FVBMeleeActivationMemory
{
	// 이번 변형 번호(누적 값)를 내주고 변형이 2개 이상일 때만 커서를 민다.
	// 무작위가 아닌 이유: 서버와 소유 클라가 통신 없이 같은 몽타주를 고른다.
	int32 TakeVariant(int32 VariantCount);

	// 서버가 실행한 시작 콤보를 기억한다. 콤보 선택 정책의 띠 순환과 회피 재개가 읽는다.
	void NoteComboStart(const UVBAttackConfig* Config, int32 Variant, EVBComboRole Role) { ComboStarts.NoteStart(Config, Variant, Role); }
	const FVBComboStartMemory& GetComboStarts() const { return ComboStarts; }

	// 회피가 끊은 콤보. 재개할 타 = 끊긴 타가 안 닿았으면 그 타, 닿았으면 다음 타.
	void NoteDodgeCut(int32 ResumeComboIndex, double Time, EVBWeaponType Weapon);
	// 유예 안, 같은 무기, 유효한 타, 섹션이 있을 때 재개할 타. 아니면 INDEX_NONE.
	int32 PendingDodgeResume(const UVBAttackConfig* Config, EVBWeaponType Weapon, const UAnimMontage* Montage, double Now) const;
	// 한 번 묻고 나면 버린다(타 번호만 지운다).
	void ConsumeDodgeResume();
	// 마지막으로 끊긴 시각(월드 초). 로그용.
	double DodgeCutTime() const { return DodgeTime; }

private:
	int32 VariantCursor = 0;
	FVBComboStartMemory ComboStarts;
	int32 DodgeComboIndex = INDEX_NONE;
	double DodgeTime = -1.0;
	EVBWeaponType DodgeWeapon = EVBWeaponType::None;
};
