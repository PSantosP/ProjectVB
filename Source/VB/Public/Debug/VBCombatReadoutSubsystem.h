// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "Subsystems/WorldSubsystem.h"
#include "VBCombatReadoutSubsystem.generated.h"

struct FVBComboChoice;
struct FVBComboSelectContext;

/**
 * 전투 판독의 장부: 콤보, 타격 창, 피격자, 넉백. 단위 시험이 월드 없이 돌리도록 서브시스템과 떼어 둔다.
 * 게임플레이 상태는 건드리지 않고, 전달받은 피격자의 위치, 속도만 읽는다.
 */
class VB_API FVBCombatReadoutLog
{
public:
	static constexpr int32 MaxCombos = 3;
	static constexpr int32 MaxHitsPerCombo = 40;

	void AddCombo(const AActor* Attacker, const FString& Header, double Now);
	// Target = 창이 열릴 때 공격자의 조준 대상(없으면 null). 방금 맞힌 적을 놓쳤는지 가리는 데 쓴다.
	void WindowOpen(const AActor* Attacker, int32 WindowKey, int32 HitNumber, const FString& Shape, float DistanceCm, float StopCm,
		const AActor* Target);
	void WindowHit(const AActor* Attacker, int32 WindowKey, const AActor* Victim);
	// bCut = 자기 노티파이 끝이 아니라 끊김(패링, 회피, 능력 종료)으로 닫힘. 로그에 쓸 줄을 돌려준다.
	FString WindowClosed(const AActor* Attacker, int32 WindowKey, bool bCut);
	void Knockback(const AActor* Victim, const AActor* Instigator, float Strength, float EstimatedCm, const TCHAR* Mode, double Now);
	void TickTracking(double Now);
	// 지난 호출 뒤 끝난 밀림의 줄들(안착, 시간 초과, 피격자 사라짐, 다음 밀림에 끊김).
	// 밀림 하나는 공격자의 다음 창이 아니라 안착할 때까지 추적한다.
	TArray<FString> TakePushLines();

	// 줄마다 글과 색, 최신 콤보부터. 서브시스템이 그리고 시험이 읽는다.
	TArray<TPair<FString, FColor>> BuildLines() const;

private:
	struct FVictimLine
	{
		TWeakObjectPtr<const AActor> Actor;
		FString Name;
		float Strength = -1.0f;
		float EstimatedCm = -1.0f;
		FString Mode;
		FVector Start = FVector::ZeroVector;
		double TrackStart = 0.0;
		float MovedCm = -1.0f;
		bool bTracking = false;
	};
	struct FHitLine
	{
		int32 WindowKey = INDEX_NONE;
		int32 HitNumber = 0;
		FString Shape;
		float DistanceCm = -1.0f;
		float StopCm = -1.0f;
		TWeakObjectPtr<const AActor> Target;
		bool bClosed = false;
		bool bCut = false;
		int32 MissAfterHit = 0; // 이 창이 놓친 대상을 앞서 HIT 한 타격 번호
		TArray<FVictimLine> Victims;
	};
	struct FComboBlock
	{
		TWeakObjectPtr<const AActor> Attacker;
		FString Header;
		double StartTime = 0.0;
		TArray<FHitLine> Hits;
	};

	FComboBlock* FindCombo(const AActor* Attacker);
	FHitLine* FindLine(const AActor* Attacker, int32 WindowKey);
	static void StopTracking(FVictimLine& Victim);
	static FString FormatLine(const FHitLine& Line);
	// 밀림 하나를 멈추고 끝난 이유와 함께 PUSH 줄을 줄 세운다.
	void EndPush(const FComboBlock& Block, const FHitLine& Line, FVictimLine& Victim, double Now, const TCHAR* Reason);

	// 최신이 앞, 최대 MaxCombos 개.
	TArray<FComboBlock> Combos;
	TArray<FString> PendingPushLines;
};

/**
 * 전투 간격을 보는 개발용 PIE 오버레이. 콤보 시작 이유, 타격 창마다 열릴 때 거리 대 정지 거리, HIT/MISS, 피격자별 넉백 예상 대 실제 이동을 보인다.
 * 보고만 받고 게임플레이는 바꾸지 않는다. `vb.Combat.Readout 1` 일 때만 켜지고 Shipping 에서는 안 만든다. 데이터는 서버 쪽이다.
 */
UCLASS()
class VB_API UVBCombatReadoutSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// 이 월드에서 판독이 켜져 있지 않으면 null. 호출부는 이 포인터 하나만 확인한다.
	static UVBCombatReadoutSubsystem* GetActive(const UObject* WorldContext);

	// 콤보 시작 머리글. 시험이 월드 없이 확인하도록 static 이다.
	static FString DescribeChoice(const AActor* Attacker, const FString& VariantName, const FVBComboChoice& Choice,
		const FVBComboSelectContext& Context);

	void NoteComboChosen(const AActor* Attacker, const FString& VariantName, const FVBComboChoice& Choice,
		const FVBComboSelectContext& Context);
	void NoteDodgeResume(const AActor* Attacker, const FString& VariantName);
	// WindowKey 는 공격자 능력 인스턴스마다 고유하다(UVBGA_MeleeAttackBase::FOpenTraceWindow::Key).
	void NoteWindowOpen(const AActor* Attacker, int32 WindowKey, int32 HitNumber, const FString& Shape, float DistanceCm, float StopCm,
		const AActor* Target);
	void NoteWindowHits(const AActor* Attacker, int32 WindowKey, const TArray<FHitResult>& Hits);
	void NoteWindowClosed(const AActor* Attacker, int32 WindowKey, bool bCut);
	// Mode: "launch"(LaunchCharacter), "clip"(피격 반응 루트모션이 밀림을 맡음), "none".
	void NoteKnockback(const AActor* Victim, const AActor* Instigator, float Strength, float EstimatedCm, const TCHAR* Mode);

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	double Now() const;

	FVBCombatReadoutLog Book;
};
