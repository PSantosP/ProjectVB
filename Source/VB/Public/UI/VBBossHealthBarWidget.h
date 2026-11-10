// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VBBossHealthBarWidget.generated.h"

class AVBBossBase;
class UPanelWidget;
class UVBBossEncounterSubsystem;
class UVBEnemyHealthBarWidget;

/**
 * 보스 바(UI.Slot.HUD.BossBar 기본 위젯). 싸우는 중인 보스의 이름과 체력을 보이고, 없으면 접힌다.
 * 구성 때 다시 구독하고 지금 값을 바로 읽는다(HUD 가 늦게 떠도 맞게).
 */
UCLASS()
class VB_API UVBBossHealthBarWidget : public UUserWidget
{
	GENERATED_BODY()

	friend class FVBEnemyBarTestAccess;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// 바가 들어갈 자리. 바는 C++ 가 BarClass 로 만들어 붙인다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UPanelWidget> BarSlot;

	// 바 생김새(WBP_EnemyHealthBar 또는 보스용 변형). 비면 바가 없다(경고).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TSubclassOf<UVBEnemyHealthBarWidget> BarClass;

private:
	void ShowBoss(AVBBossBase* Boss);

	// 재구성돼도 하나다(NativeOnInitialized 에서 만든다).
	UPROPERTY(Transient)
	TObjectPtr<UVBEnemyHealthBarWidget> BossBar;

	TWeakObjectPtr<UVBBossEncounterSubsystem> Encounters;
	FDelegateHandle EngagedBossChangedHandle;
};
