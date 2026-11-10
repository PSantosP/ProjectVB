// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "VBGameMode.generated.h"

class UVBCinematicDefinition;

/**
 * VB 기본 GameMode. 전투 규칙과 플레이어 시작을 관리한다.
 * 플레이어 시작의 절차와 유효성 판정은 FVBPlayerStartAttempt 가 맡는다.
 */
UCLASS()
class VB_API AVBGameMode : public AGameModeBase
{
	GENERATED_BODY()
	
public:
	AVBGameMode();

	/** 전투 피해로 플레이어가 사망하지 않는 규칙인지 반환 */
	bool IsPlayerCombatNonLethal() const { return bNonLethalPlayerCombat; }
	
protected:
	/** 플레이어의 전투 피해 체력을 최소 1로 유지 */
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	bool bNonLethalPlayerCombat = false;

	// 월드의 액터가 초기화되기 전(서버). 메뉴 없이 열린 월드면 시작 기록을 "바로 시작" 으로 만든다 -
	// 난이도, 이야기 컴포넌트가 초기화 때 새 세이브를 보게 한다.
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

	// 폰 스폰 직후(서버) 호출. Continue 로드면 세이브의 빌드/위치를 새 폰에 적용한다.
	// OpenLevel 이 월드를 파괴하므로 세이브 서브시스템의 적용 대기 표시는 새 레벨에서 처음 폰이 생기는 이 지점이 소비한다.
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

	UPROPERTY(EditDefaultsOnly, Category="VB|Cinematic")
	bool bPlayInitialNewGameCinematic = false;

	UPROPERTY(EditDefaultsOnly, Category="VB|Cinematic")
	TObjectPtr<UVBCinematicDefinition> InitialNewGameCinematic;
};
