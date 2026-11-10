// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;

// FVBPlayBotChecks - 체크리스트 시나리오의 PASS/FAIL 줄("check")과 눈으로 보는 구간("look")을 낸다
// 관찰하지 못한 검사는 unavailable(pass:null)이고 통과로 세지 않는다
// 반대 증명: -VBPlayBotForceWrong=<a,b> 에 든 검사는 반대를 기대하므로 올바른 게임이면 FAIL 이어야 한다
class FVBPlayBotChecks
{
public:
	TFunction<void(const FString& Json)> Emit;

	void SetForceWrong(const FString& CommaList);
	// 실행의 프레임 카운터 - 프레임 없이(< 0) 받은 검사는 -1 대신 현재 프레임을 찍는다
	void SetClock(const int64* InNow) { Now = InNow; }

	// Observed == Expected 이면 통과 (강제 이름은 반대) - EvidenceJson 은 JSON 객체나 빈 값
	void Expect(const FString& Name, bool bObserved, bool bExpected, int64 Frame, const FString& EvidenceJson = FString());
	// 동작, 처형 시나리오가 같이 쓰는 조립 판정
	void ExpectSCSComponent(AActor* Owner, UClass* Type, FName ExpectedName, const UObject* Found, const FString& Name,
		int64 Frame, bool bRequireReplicated = false);
	void Unavailable(const FString& Name, int64 Frame, const FString& Why);
	// 사용자가 눈으로 판단하는 구간 - contact sheet 가 되고 PASS 가 되지 않는다
	void Look(const FString& Name, int64 FromFrame, int64 ToFrame, const FString& Note);

	int32 GetPassed() const { return Passed; }
	int32 GetFailed() const { return Failed; }
	int32 GetUnavailable() const { return Unknown; }
	int32 GetLooks() const { return Looks; }
	// 검사가 하나 이상 돌고 모두 통과해야 "pass", 검사가 없는 시나리오는 "measured"
	FString StatusWord() const;
	// 어느 검사도 쓰지 않은 강제 이름은 반대 실행을 증명된 것처럼 보이게 하므로 unavailable 로 보고한다
	void ReportUnusedForceWrong(int64 Frame);

private:
	int64 Stamp(int64 Frame) const { return Frame >= 0 || !Now ? Frame : *Now; }

	const int64* Now = nullptr;
	TSet<FString> ForceWrong;
	TSet<FString> Evaluated;
	int32 Passed = 0;
	int32 Failed = 0;
	int32 Unknown = 0;
	int32 Looks = 0;
};
