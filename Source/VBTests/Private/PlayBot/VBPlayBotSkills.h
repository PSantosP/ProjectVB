// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "PlayBot/VBPlayBotScenarioBase.h"

class SWindow;

class APawn;

// FVBPlayBotSkill - 시나리오가 이어 붙이는 사람 동작 하나. 모든 타이밍은 사람 모델을 거친다
// 지금 본 신호는 화면 표시 프레임 + 뽑은 반응 시간 뒤에 행동하고, 누름도 뽑은 시간만큼 유지한다
class FVBPlayBotSkill
{
public:
	explicit FVBPlayBotSkill(FString InName) : Name(MoveTemp(InName)) {}
	virtual ~FVBPlayBotSkill() = default;
	virtual void Begin(FVBPlayBotContext& Ctx) {}
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) = 0;

	const FString& GetName() const { return Name; }
	const FString& GetFailure() const { return Failure; }

protected:
	EVBPlayBotState Fail(const FString& Reason)
	{
		Failure = Reason;
		return EVBPlayBotState::Failed;
	}

	FString Name;
	FString Failure;
};

// FVBPlayBotSequence - skill 을 순서대로 돌리고 하나라도 실패하면 전체가 실패한다. 프레임과 함께 "step" 이벤트(begin/end/fail)를 남긴다
class FVBPlayBotSequence
{
public:
	FVBPlayBotSequence& Add(TUniquePtr<FVBPlayBotSkill> Skill);
	template <typename T, typename... Args>
	FVBPlayBotSequence& Then(Args&&... InArgs) { return Add(MakeUnique<T>(Forward<Args>(InArgs)...)); }

	EVBPlayBotState Tick(FVBPlayBotContext& Ctx);
	const FString& GetFailure() const { return Failure; }
	bool IsEmpty() const { return Steps.IsEmpty(); }
	void Reset();

private:
	TArray<TUniquePtr<FVBPlayBotSkill>> Steps;
	int32 Index = 0;
	bool bBegun = false;
	FString Failure;
};

// FVBSkillDo - 람다를 한 번 돌리는 즉시 단계(준비, 검사, 덮어쓰기). false 를 돌려주면 OutError 로 실패한다
class FVBSkillDo : public FVBPlayBotSkill
{
public:
	FVBSkillDo(FString InName, TFunction<bool(FVBPlayBotContext&, FString&)> InFn) : FVBPlayBotSkill(MoveTemp(InName)), Fn(MoveTemp(InFn)) {}
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override;
private:
	TFunction<bool(FVBPlayBotContext&, FString&)> Fn;
};

class FVBSkillWait : public FVBPlayBotSkill
{
public:
	FVBSkillWait(FString InName, double InSeconds) : FVBPlayBotSkill(MoveTemp(InName)), Seconds(InSeconds) {}
	virtual void Begin(FVBPlayBotContext& Ctx) override { StartFrame = Ctx.Frame; }
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override;
private:
	double Seconds = 0.0;
	int64 StartFrame = 0;
};

// FVBSkillWaitUntil - 조건이 맞을 때까지 기다리고 TimeoutSeconds 가 지나면 단계 이름을 담아 실패한다
class FVBSkillWaitUntil : public FVBPlayBotSkill
{
public:
	FVBSkillWaitUntil(FString InName, TFunction<bool(FVBPlayBotContext&)> InCondition, double InTimeoutSeconds)
		: FVBPlayBotSkill(MoveTemp(InName)), Condition(MoveTemp(InCondition)), TimeoutSeconds(InTimeoutSeconds) {}
	virtual void Begin(FVBPlayBotContext& Ctx) override { StartFrame = Ctx.Frame; }
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override;
private:
	TFunction<bool(FVBPlayBotContext&)> Condition;
	double TimeoutSeconds = 0.0;
	int64 StartFrame = 0;
};

// FVBSkillPress - 사람 반응 시간 뒤에 패드 버튼을 누른다. Action 은 pawn/controller 의 UInputAction 속성 이름, 메뉴는 raw key
// HoldSeconds <= 0 이면 누름 유지 시간을 profile 에서 뽑는다
class FVBSkillPress : public FVBPlayBotSkill
{
public:
	FVBSkillPress(FString InName, FName InAction, double InHoldSeconds = 0.0)
		: FVBPlayBotSkill(MoveTemp(InName)), Action(InAction), HoldSeconds(InHoldSeconds) {}
	FVBSkillPress(FString InName, FKey InKey, double InHoldSeconds = 0.0)
		: FVBPlayBotSkill(MoveTemp(InName)), RawKey(InKey), HoldSeconds(InHoldSeconds) {}
	virtual void Begin(FVBPlayBotContext& Ctx) override;
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override;
private:
	FName Action;
	FKey RawKey;
	double HoldSeconds = 0.0;
	int32 PressId = INDEX_NONE;
	FString BeginError;
};

// FVBSkillApproach - 사람처럼 방향을 고쳐 가며 actor 쪽으로 걸어 Range(2D) 안에 들면 stick 을 놓는다
class FVBSkillApproach : public FVBPlayBotSkill
{
public:
	FVBSkillApproach(FString InName, TFunction<AActor*(FVBPlayBotContext&)> InTarget, double InRange, double InTimeoutSeconds)
		: FVBPlayBotSkill(MoveTemp(InName)), TargetFn(MoveTemp(InTarget)), Range(InRange), TimeoutSeconds(InTimeoutSeconds) {}
	virtual void Begin(FVBPlayBotContext& Ctx) override { StartFrame = Ctx.Frame; NextCorrection = Ctx.Frame; }
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override;
private:
	TFunction<AActor*(FVBPlayBotContext&)> TargetFn;
	double Range = 0.0;
	double TimeoutSeconds = 0.0;
	int64 StartFrame = 0;
	int64 NextCorrection = 0;
};

// FVBSkillApproachPoint - 월드 지점(사람 보정 간격마다 다시 계산)으로 걸어 Tolerance 안에 들면 stick 을 놓는다
class FVBSkillApproachPoint : public FVBPlayBotSkill
{
public:
	FVBSkillApproachPoint(FString InName, TFunction<FVector(FVBPlayBotContext&)> InPoint, double InTolerance, double InTimeoutSeconds)
		: FVBPlayBotSkill(MoveTemp(InName)), PointFn(MoveTemp(InPoint)), Tolerance(InTolerance), TimeoutSeconds(InTimeoutSeconds) {}
	virtual void Begin(FVBPlayBotContext& Ctx) override { StartFrame = Ctx.Frame; NextCorrection = Ctx.Frame; }
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override;
private:
	TFunction<FVector(FVBPlayBotContext&)> PointFn;
	double Tolerance = 0.0;
	double TimeoutSeconds = 0.0;
	int64 StartFrame = 0;
	int64 NextCorrection = 0;
};

// FVBSkillMove - 왼쪽 stick 을 정해진 시간 동안 민다. 방향은 람다의 월드 방향(보정 간격마다 다시 계산), 세기는 profile 값
class FVBSkillMove : public FVBPlayBotSkill
{
public:
	FVBSkillMove(FString InName, TFunction<FVector(FVBPlayBotContext&)> InDirection, double InSeconds)
		: FVBPlayBotSkill(MoveTemp(InName)), DirectionFn(MoveTemp(InDirection)), Seconds(InSeconds) {}
	virtual void Begin(FVBPlayBotContext& Ctx) override { StartFrame = Ctx.Frame; NextCorrection = Ctx.Frame; }
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override;
private:
	TFunction<FVector(FVBPlayBotContext&)> DirectionFn;
	double Seconds = 0.0;
	int64 StartFrame = 0;
	int64 NextCorrection = 0;
};

// FVBSkillLook - 오른쪽 stick(카메라)을 Seconds 동안 Stick 값으로 두었다가 가운데로 돌린다
class FVBSkillLook : public FVBPlayBotSkill
{
public:
	FVBSkillLook(FString InName, FVector2D InStick, double InSeconds) : FVBPlayBotSkill(MoveTemp(InName)), Stick(InStick), Seconds(InSeconds) {}
	virtual void Begin(FVBPlayBotContext& Ctx) override;
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override;
private:
	FVector2D Stick = FVector2D::ZeroVector;
	double Seconds = 0.0;
	int64 StartFrame = 0;
};

// FVBSkillAttackUntil - 사람처럼 사거리를 지키며 공격을 누르고, 적중을 볼 때마다 반응 뒤 다시 누른다
// Done(ctx, hitsSinceBegin) 이 true 가 되면 끝나고, TimeoutSeconds 가 지나면 실패한다
class FVBSkillAttackUntil : public FVBPlayBotSkill
{
public:
	// MashSeconds > 0 이면 적중을 기다리지 않고 MashSeconds 마다 눌러 콤보를 일부러 잇는다. 0 이면 적중에만 반응한다
	FVBSkillAttackUntil(FString InName, TFunction<AActor*(FVBPlayBotContext&)> InTarget, FName InAttack, double InRange,
		TFunction<bool(FVBPlayBotContext&, int32)> InDone, double InTimeoutSeconds, double InMashSeconds = 0.0)
		: FVBPlayBotSkill(MoveTemp(InName)), TargetFn(MoveTemp(InTarget)), Attack(InAttack), Range(InRange), Done(MoveTemp(InDone)),
			TimeoutSeconds(InTimeoutSeconds), MashSeconds(InMashSeconds) {}
	virtual void Begin(FVBPlayBotContext& Ctx) override;
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override;
private:
	TFunction<AActor*(FVBPlayBotContext&)> TargetFn;
	FName Attack;
	double Range = 0.0;
	TFunction<bool(FVBPlayBotContext&, int32)> Done;
	double TimeoutSeconds = 0.0;
	double MashSeconds = 0.0;
	int64 StartFrame = 0;
	int32 BaseHits = 0;
	int32 SeenHits = 0;
	int32 PressId = INDEX_NONE;
	int64 NextCorrection = 0;
	int64 LastPressFrame = -1;
};

// FVBSkillPadMode - 마우스 메뉴 뒤 패드로 돌아간다. CommonInput 이 Gamepad 를 알리고 패드 mapping context 가 다시 걸릴 때까지 stick 을 살짝 민다
class FVBSkillPadMode : public FVBPlayBotSkill
{
public:
	FVBSkillPadMode(double InTimeoutSeconds) : FVBPlayBotSkill(TEXT("pad mode")), TimeoutSeconds(InTimeoutSeconds) {}
	virtual void Begin(FVBPlayBotContext& Ctx) override { StartFrame = Ctx.Frame; ReadyFrame = -1; }
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override;
private:
	double TimeoutSeconds = 0.0;
	int64 StartFrame = 0;
	int64 ReadyFrame = -1;
};

// FVBSkillEquip - 무기가 아직 안 나와 있으면 플레이어처럼 슬롯 버튼으로 꺼낸다
class FVBSkillEquip : public FVBPlayBotSkill
{
public:
	FVBSkillEquip(FString InWeapon, FName InSlotAction) : FVBPlayBotSkill(TEXT("equip ") + InWeapon), Weapon(MoveTemp(InWeapon)), SlotAction(InSlotAction) {}
	virtual void Begin(FVBPlayBotContext& Ctx) override { StartFrame = Ctx.Frame; bPressed = false; }
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override;
private:
	FString Weapon;
	FName SlotAction;
	int64 StartFrame = 0;
	bool bPressed = false;
};

// FVBSkillMenuSeek - 포커스된 widget 이 Labels 중 하나를 보일 때까지 StepKey 를 사람 타이밍으로 누른다
// MaxSteps 번을 넘으면 실패한다. Labels 는 ini 데이터이고 부분 문자열로 맞춘다
class FVBSkillMenuSeek : public FVBPlayBotSkill
{
public:
	FVBSkillMenuSeek(FString InName, TArray<FString> InLabels, FKey InStepKey, int32 InMaxSteps)
		: FVBPlayBotSkill(MoveTemp(InName)), Labels(MoveTemp(InLabels)), StepKey(InStepKey), MaxSteps(InMaxSteps) {}
	// 누를 키를 실행 중에 찾는 판(CommonUI accept/back). 예: 값이 보일 때까지 "next" 를 누른다
	FVBSkillMenuSeek(FString InName, TArray<FString> InLabels, TFunction<bool(FVBPlayBotContext&, FKey&, FString&)> InResolve,
		int32 InMaxSteps)
		: FVBPlayBotSkill(MoveTemp(InName)), Labels(MoveTemp(InLabels)), Resolve(MoveTemp(InResolve)), MaxSteps(InMaxSteps) {}
	virtual void Begin(FVBPlayBotContext& Ctx) override;
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override;
private:
	TArray<FString> Labels;
	TFunction<bool(FVBPlayBotContext&, FKey&, FString&)> Resolve;
	FString BeginError;
	FKey StepKey;
	int32 MaxSteps = 0;
	int32 Steps = 0;
	int32 PressId = INDEX_NONE;
	int64 ReadyFrame = 0;
};

// FVBSkillMenuSteer - 화면에 보이는 Labels 글자의 위치를 강조 항목과 비교해 D-pad 를 한 번씩 그쪽으로 누른다
// 강조가 그 글자에 올 때까지 반복하므로 Blueprint 배치를 몰라도 된다
class FVBSkillMenuSteer : public FVBPlayBotSkill
{
public:
	FVBSkillMenuSteer(FString InName, TArray<FString> InLabels, int32 InMaxSteps)
		: FVBPlayBotSkill(MoveTemp(InName)), Labels(MoveTemp(InLabels)), MaxSteps(InMaxSteps) {}
	virtual void Begin(FVBPlayBotContext& Ctx) override { Steps = 0; PressId = INDEX_NONE; ReadyFrame = Ctx.Frame; }
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override;
private:
	TArray<FString> Labels;
	int32 MaxSteps = 0;
	int32 Steps = 0;
	int32 PressId = INDEX_NONE;
	int64 ReadyFrame = 0;
};

// FVBSkillMenuClick - PC 플레이어처럼 보이는 글자를 찾아 사람 이동 시간으로 포인터를 옮기고 클릭한다
// 합성 Slate pointer 이벤트만 쓰므로 실제 OS 커서는 움직이지 않는다
class FVBSkillMenuClick : public FVBPlayBotSkill
{
public:
	// label 은 "Target@Anchor"(Anchor 줄의 Target 글자)일 수 있다. Until 이 있으면 Anchor 줄이 Until 을 보일 때까지 최대 MaxClicks 번 다시 클릭한다
	FVBSkillMenuClick(FString InName, TArray<FString> InLabels, double InTimeoutSeconds, FString InUntil = FString(), int32 InMaxClicks = 1)
		: FVBPlayBotSkill(MoveTemp(InName)), Labels(MoveTemp(InLabels)), TimeoutSeconds(InTimeoutSeconds), Until(MoveTemp(InUntil)), MaxClicks(InMaxClicks) {}
	virtual void Begin(FVBPlayBotContext& Ctx) override;
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override;
private:
	void SendMove(const FVector2D& To);
	bool Locate(FVector2D& OutCentre, TArray<FString>* OutRow) const;
	TArray<FString> Labels;
	double TimeoutSeconds = 0.0;
	FString Until;
	int32 MaxClicks = 1;
	int32 Clicks = 0;
	int64 ReadyFrame = 0;
	int64 StartFrame = 0;
	int64 MoveStart = -1;
	int64 MoveFrames = 1;
	int64 DownFrame = -1;
	int64 UpFrame = -1;
	FVector2D From = FVector2D::ZeroVector;
	FVector2D Target = FVector2D::ZeroVector;
	FVector2D Last = FVector2D::ZeroVector;
};

// FVBSkillPressResolved - 실행 중에 찾은 키(CommonUI accept/back, widget 의 action)를 FVBSkillPress 와 같은 사람 타이밍으로 누른다
class FVBSkillPressResolved : public FVBPlayBotSkill
{
public:
	FVBSkillPressResolved(FString InName, TFunction<bool(FVBPlayBotContext&, FKey&, FString&)> InResolve)
		: FVBPlayBotSkill(MoveTemp(InName)), Resolve(MoveTemp(InResolve)) {}
	virtual void Begin(FVBPlayBotContext& Ctx) override;
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override;
private:
	TFunction<bool(FVBPlayBotContext&, FKey&, FString&)> Resolve;
	int32 PressId = INDEX_NONE;
	FString BeginError;
};

namespace VBPlayBotSkills
{
	// AddMenuPath - ini 줄로 쓴 메뉴 경로를 Sequence 에 덧붙인다. 메뉴 배치가 Blueprint 에 있어서 경로도 데이터로 둔다
	// 줄 종류는 Seek, Steer, Click, ClickUntil, Press, PadMode, WaitWidget, WaitNoWidget, Wait 이고 칸은 | 로 나눈다
	// $Name 은 Vars 로 바꾼다(ini 가 {...} 를 지워서 $ 를 쓴다). 잘못된 줄은 OutError 에 담아 false
	bool AddMenuPath(FVBPlayBotSequence& Sequence, const TArray<FString>& Lines, const TMap<FString, FString>& Vars, FString& OutError);
	// FindRowTarget - Spec 은 "Text" 또는 "Target@Anchor"(Anchor 줄에서 가장 가까운 Target). OutRowTexts 에 Anchor 줄 글자를 담는다
	bool FindRowTarget(const TSharedRef<SWindow>& Window, const FString& Spec, FVector2D& OutCentre, TArray<FString>* OutRowTexts);
	// FocusTexts - 포커스된 항목에서 사람이 보는 글자. 포커스된 Slate widget 과 부모 아래의 STextBlock 들
	TArray<FString> FocusTexts();
	// CommonUIKey - 프로젝트 CommonUI 입력 설정에서 패드의 accept(bBack=false) 또는 back 키를 찾는다
	bool CommonUIKey(FVBPlayBotContext& Ctx, bool bBack, FKey& OutKey, FString& OutError);
	// ReactPress - CueFrame 에 본 신호에 사람 반응 시간 뒤 누름을 예약하고 press id 또는 INDEX_NONE 을 돌려준다
	int32 ReactPress(FVBPlayBotContext& Ctx, FName Action, const FKey& RawKey, const FString& Cue, int64 CueFrame, double HoldSeconds);
	// TurnCameraToward - 오른쪽 stick 으로 카메라를 Target 쪽으로 돌리고 AlignedDeg 안에 들면 true
	// YawOffsetDeg 만큼 Target 을 지나쳐 돌린다(양수 = 오른쪽). 예: 대상을 pawn 옆 화면 1/3 자리에 둘 때
	bool TurnCameraToward(FVBPlayBotContext& Ctx, const AActor* Target, double AlignedDeg = 20.0, double YawOffsetDeg = 0.0);
	// EnsureAction - action(pawn/controller 속성)의 패드 키를 실행마다 한 번 찾는다
	bool EnsureAction(FVBPlayBotContext& Ctx, FName Action, FString& OutError);
	// SpawnStillTarget - 플레이어 앞 Distance cm 에 ClassPath 를 마주 보게 스폰하고 AI logic 을 멈춘다(연습 대상)
	ACharacter* SpawnStillTarget(FVBPlayBotContext& Ctx, const FString& ClassPath, double Distance, FString& OutError);
	// StopAI - pawn 의 AI logic 을 멈추고, brain 이 계속 돌면 false
	bool StopAI(APawn* Pawn);
}
