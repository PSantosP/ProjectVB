// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VBMapSettings.generated.h"

class UVBMapDefinition;
class UWorld;

/** 맵 하나와 그 지도 정의. */
USTRUCT()
struct FVBMapEntry
{
	GENERATED_BODY()

	UPROPERTY(config, EditAnywhere, Category="VB|Map")
	TSoftObjectPtr<UWorld> World;

	UPROPERTY(config, EditAnywhere, Category="VB|Map")
	TSoftObjectPtr<UVBMapDefinition> Definition;
};

/**
 * 어느 맵이 어느 지도 정의를 쓰나. Project Settings > Game > VB Map, 저장은 Config/DefaultGame.ini.
 * 맵이 목록에 없으면 지도가 없는 맵이다(미니맵, 지도가 숨는다) - 시험 맵, 메뉴 맵이 그렇다.
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="VB Map"))
class VB_API UVBMapSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(config, EditAnywhere, Category="VB|Map")
	TArray<FVBMapEntry> Maps;

	// 이 월드의 지도 정의. PIE 접두어를 걷고 패키지 이름으로 맞춘다(대소문자 무시). 없거나 쓸 수 없으면 nullptr(조용히 - 지도 없는 맵이 정상이다).
	//   자산을 동기로 읽는다 - 매 프레임 부르지 말고 부르는 쪽이 월드마다 한 번 받아 둔다.
	static const UVBMapDefinition* FindForWorld(const UWorld* World);

	// 패키지 이름(PIE 접두어 없는 것)으로 찾는다. 시험과 FindForWorld 가 같이 쓴다.
	const UVBMapDefinition* FindForPackage(const FString& PackageName) const;
};
