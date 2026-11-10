// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Misc/ConfigCacheIni.h"
#include "PlayBot/VBPlayBotHumanModel.h"

// 불러온 object 의 속성을 실행 중에만 바꾼다(-game 은 package 를 저장하지 못해 저장하지 않는다). "Authored" 는 원래 값을 둔다
struct FVBPlayBotVariant
{
	FString Name;
	FString ObjectPath;
	FName Property;
	FString Value;

	bool IsAuthored() const { return ObjectPath.IsEmpty(); }
	// 형식은 "Name" 또는 "Name|ObjectPath|Property|Value"
	bool Parse(const FString& Text, FString& OutError);
};

// 모든 시나리오 구역이 가진 키. 시나리오별 키는 시나리오가 Get* 으로 읽는다
// 파일은 Config/DefaultPlayBot.ini 의 [Scenario.<Name>] 구역이고 필수 키가 없으면 불러오기가 실패한다
struct FVBPlayBotRunConfig
{
	FString Name;
	FString Section;
	FName ClassName;
	FString Map;
	int32 Seed = 0;
	double MaxRunSeconds = 900.0;
	TArray<FGameplayTag> TrackTags;
	FVBPlayBotHumanProfile Profile;

	bool Load(const FString& ScenarioName, FString& OutError);
	FString ToJson() const;

	FString GetString(const TCHAR* Key, const FString& Default = FString()) const;
	double GetDouble(const TCHAR* Key, double Default) const;
	int32 GetInt(const TCHAR* Key, int32 Default) const;
	TArray<FString> GetArray(const TCHAR* Key) const;
	// 키가 없거나 비면 구역과 키 이름을 담은 메시지로 실패한다
	bool RequireString(const TCHAR* Key, FString& Out, FString& OutError) const;

private:
	FConfigFile Config;
};

// 한 실행에서 원래 값 되돌리기의 결과. 못 되돌린 값은 다음 실행을 바뀐 값으로 시작시키므로 실패이고, 사라진 object 는 확인 불가일 뿐이다
struct FVBPlayBotRestoreReport
{
	int32 Restored = 0;
	TArray<FString> Unavailable;
	TArray<FString> Failures;

	bool HasEntries() const { return Restored > 0 || Unavailable.Num() > 0 || Failures.Num() > 0; }
	FString ToJson() const;
};

// variant 값을 적용하고, 실행이 되돌릴 수 있게 원래 글자를 기억한다
class FVBPlayBotOverrides
{
public:
	bool Apply(const FVBPlayBotVariant& Variant, FString& OutReadback, FString& OutError);
	// 기억한 값을 모두 되써 넣고 다시 읽는다. 결과는 GetRestoreReport 에 남는다
	void RestoreAll();
	const FVBPlayBotRestoreReport& GetRestoreReport() const { return Report; }

private:
	friend class FVBPlayBotOverridesTestAccess;

	struct FOriginal
	{
		TWeakObjectPtr<UObject> Object;
		FString Path;
		FName Property;
		FString Text;
	};
	TArray<FOriginal> Originals;
	FVBPlayBotRestoreReport Report;
};
