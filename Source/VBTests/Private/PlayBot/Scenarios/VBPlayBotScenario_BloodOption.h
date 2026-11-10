// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PlayBot/VBPlayBotScenarioBase.h"
#include "PlayBot/VBPlayBotSkills.h"

// 피 옵션 시나리오 - 패드로 Options > Blood 를 모드마다 바꾸고 선 적을 목록의 무기로 친다.
// Off 면 상처/decal 은 없고 타격 섬광은 남아야 한다. 타격 때마다 화면을 확인한다.
class FVBPlayBotScenario_BloodOption : public FVBPlayBotScenarioBase
{
public:
	virtual bool Setup(FVBPlayBotContext& Ctx, FString& OutError) override;
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override;

private:
	struct FHitWindow
	{
		FString Mode;
		FString Weapon;
		bool bHit = false;
		int64 HitFrame = -1;
		int32 DecalsAtHit = 0;
		int32 MaxDecalDelta = 0;
		TArray<FString> FX;
	};

	void Evaluate(FVBPlayBotContext& Ctx);
	const FHitWindow* Find(const FString& Mode, const FString& Weapon) const;

	FVBPlayBotSequence Sequence;
	TWeakObjectPtr<ACharacter> Target;
	TArray<FHitWindow> Windows;
	int32 Active = INDEX_NONE;
	int32 WindowFrames = 60;
	TArray<FString> Weapons;
	bool bEvaluated = false;
};
