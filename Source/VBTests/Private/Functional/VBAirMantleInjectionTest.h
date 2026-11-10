// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBFunctionalTestBase.h"
#include "VBAirMantleInjectionTest.generated.h"

class UInputAction;

// AVBAirMantleInjectionTest - Enhanced Input 연속 주입으로 점프 -> 공중 맨틀 -> AIR 트래버설 -> release 의 진짜 키 경로를 헤드리스로 검증한다.
// 시작점은 벽에서 약 150cm(지상 트레이스가 빗나가 실점프), 전진 입력은 공중에서만 준다(지상 전진은 GROUND 트래버설이 선점).
// IA 는 풀패스로 찾는다(같은 이름 IA 가 있다). 피크 z 는 Tick 에서 표본화한다.
UCLASS()
class AVBAirMantleInjectionTest : public AVBFunctionalTestBase
{
	GENERATED_BODY()

public:
	AVBAirMantleInjectionTest();

	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	// 풀패스 디폴트 - 생성자에서 설정. 맵 인스턴스에서 교체 가능(EditAnywhere).
	UPROPERTY(EditAnywhere, Category="VB|Test")
	TSoftObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditAnywhere, Category="VB|Test")
	FVector StartLocation = FVector(-1050.0, 0.0, 90.15);

	UPROPERTY(EditAnywhere, Category="VB|Test")
	double StartYaw = 180.0;

	// 점프 키 홀드 시간 - bJumpInputHeld 공중맨틀 게이트 구동 (1.5s = 증명값).
	UPROPERTY(EditAnywhere, Category="VB|Test")
	float HoldSeconds = 1.5f;

	// 2.0m 벽 상단 기준 피크 z (증명값 290.2 - 여유 285).
	UPROPERTY(EditAnywhere, Category="VB|Test")
	double MinPeakZ = 285.0;

private:
	void StopInjection();
	void PollCompletion();

	// GC-only 내부 참조 - 주입 지속 동안 IA 수명 보장 (bare UPROPERTY: Category 금지 규칙).
	UPROPERTY()
	TObjectPtr<UInputAction> ResolvedAction;

	bool bDriving = false; // StartTest 완료 후에만 Tick 로직 가동
	bool bInjectionStopped = false;
	double MaxZ = 0.0;
	double Elapsed = 0.0;
	int32 DroveFrames = 0; // 공중 드라이브 발화 횟수 - "점프 자체가 안 일어남" 진단 분리용
	TUniquePtr<FVBScopedLogCapture> LogCapture;
	FTimerHandle StopHandle;
	FTimerHandle PollHandle;
};
