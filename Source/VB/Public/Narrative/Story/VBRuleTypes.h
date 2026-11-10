// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "VBRuleTypes.generated.h"

class AActor;
class UWorld;
class APlayerState;
class UVBStoryStateComponent;

// 받아들인 뒤 나중에 끝나는 효과(장면)가 끝을 한 번 알린다. 참 = 완료, 건너뛰기.
DECLARE_DELEGATE_OneParam(FVBOnDeferredEffectClosed, bool /*bCompleted*/);

/**
 * 규칙 한 번의 문맥. 부르는 쪽 스택에만 살아 UObject 참조를 붙들지 않는다.
 * 규칙은 이 문맥이 가리키는 것만 읽고 쓰며 스스로 월드를 뒤지지 않는다.
 */
struct VB_API FVBRuleContext
{
	UWorld* World = nullptr;

	// 규칙을 부른 플레이어(선택지를 고른 사람, 퀘스트를 가진 사람). 평판처럼 플레이어의 것을 읽고 쓸 때 쓴다.
	APlayerState* Instigator = nullptr;

	// 세계 사실을 관리하는 컴포넌트. 보통 AVBGameState 의 것(UVBStoryStateComponent::Get).
	UVBStoryStateComponent* Story = nullptr;

	// 부른 플레이어가 상대하는 액터(말을 건 NPC). 대화가 넣고, 상점 열기 효과가 상인으로 쓴다. 상대가 없는 자리(퀘스트 보상, 아이템 사용)는 비어 있다.
	AActor* Counterpart = nullptr;

	// 나중에 끝나는 효과가 결과를 알릴 곳(영역이 채운다). 비면 아무도 기다리지 않는다.
	FVBOnDeferredEffectClosed OnDeferredClosed;

	// 바로 끝나는 효과가 실패를 알릴 곳(부르는 쪽 스택의 bool). 비어 있어도 효과는 경고를 남긴다.
	bool* FailureSink = nullptr;
	void ReportEffectFailure() const { if (FailureSink) *FailureSink = true; }

	// 효과를 적용해도 되는가(서버, 싱글). 클라에서 효과가 돌면 사실이 서버와 갈린다. 월드가 없으면(단위 시험) 적용한다 - 복제할 곳도 없다.
	bool CanApplyEffects() const;

	// 보통의 문맥: 그 월드 + 그 월드의 세계 사실(GameState) + 부른 플레이어. 인벤토리, 대화가 같은 것을 만든다.
	static FVBRuleContext Make(const UObject* WorldContextObject, APlayerState* InInstigator);
};

/** 조건. 바탕형은 참(조건 없는 자리의 기본)이고 파생형이 Evaluate 를 덮는다. */
USTRUCT(BlueprintType)
struct VB_API FVBCondition
{
	GENERATED_BODY()

	virtual ~FVBCondition() = default;
	virtual bool Evaluate(const FVBRuleContext& Context) const { return true; }
	// 저작 값이 틀렸으면 그 이유, 맞으면 빈 문자열. 자산 검증이 오류로 적고, 틀린 조건은 실행 때 거짓이다.
	virtual FString GetDataProblem() const { return FString(); }
	// 저장되지 않는 영수증으로 장면 완료를 읽으면 그 장면, 아니면 null.
	virtual const UObject* GetTransientSceneRead() const { return nullptr; }
};

/** 효과. 서버에서만 불린다(VBRules::ApplyAll 이 거른다). 바탕형은 아무것도 안 한다. */
USTRUCT(BlueprintType)
struct VB_API FVBEffect
{
	GENERATED_BODY()

	virtual ~FVBEffect() = default;
	// 수량을 바꾸기 전에 현재 문맥에서 적용 가능한지 묻는다. 상태를 바꾸지 않고, 이후 효과의 결과까지 보장하지는 않는다.
	virtual bool CanApply(const FVBRuleContext& Context) const { return true; }
	virtual void Apply(const FVBRuleContext& Context) const {}
	// 이 효과가 지금 조종 중인 플레이어를 가져가는가(카메라, 입력). 그런 효과를 거는 쪽은 플레이어가 준비된 뒤에만 건다(영역 검증이 막는다).
	virtual bool NeedsReadyPlayer() const { return false; }
	// 받아들인 뒤 나중에 끝나며 그 결과를 OnDeferredClosed 로 한 번 알리는가(장면).
	virtual bool ReportsDeferredResult() const { return false; }
	// 이 효과가 트는 장면, 아니면 null.
	virtual const UObject* GetPlayedScene() const { return nullptr; }
};

// ---------------------------------------------------------------- 조건

/** 사실이 켜져 있나(bNegate 면 꺼져 있나). */
USTRUCT(BlueprintType, meta=(DisplayName="사실"))
struct VB_API FVBCondition_Fact : public FVBCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story", meta=(Categories="Story"))
	FGameplayTag Fact;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story")
	bool bNegate = false;

	virtual bool Evaluate(const FVBRuleContext& Context) const override;
};

UENUM(BlueprintType)
enum class EVBRuleCompare : uint8
{
	AtLeast,
	AtMost,
	Equal,
};

/** 카운터가 값과 비교해 맞나(없는 카운터 = 0). */
USTRUCT(BlueprintType, meta=(DisplayName="카운터"))
struct VB_API FVBCondition_Counter : public FVBCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story", meta=(Categories="Story"))
	FGameplayTag Counter;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story")
	EVBRuleCompare Compare = EVBRuleCompare::AtLeast;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story")
	int32 Value = 1;

	virtual bool Evaluate(const FVBRuleContext& Context) const override;
};

/** 목록에서 켜진 사실의 수가 값과 비교해 맞나. 수는 저장하지 않고 읽을 때 센다 - 개별 사실이 단 하나의 진실이다. */
USTRUCT(BlueprintType, meta=(DisplayName="사실 개수"))
struct VB_API FVBCondition_FactCount : public FVBCondition
{
	GENERATED_BODY()

	// 정확한 태그로 센다(부모 태그는 자식을 세지 않는다). 비거나 겹치면 데이터 오류다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story", meta=(Categories="Story"))
	TArray<FGameplayTag> Facts;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story")
	EVBRuleCompare Compare = EVBRuleCompare::AtLeast;

	// 0 ~ 사실 수 안이어야 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story")
	int32 Value = 1;

	virtual bool Evaluate(const FVBRuleContext& Context) const override;
	virtual FString GetDataProblem() const override;
};

// 평판 조건은 Player/VBPlayerRules 에 있다 - 규칙 핵심이 GAS 를 보지 않게(설계 13.1절 2번).

// ---------------------------------------------------------------- 효과

/** 사실을 켜거나 끈다. */
USTRUCT(BlueprintType, meta=(DisplayName="사실 바꾸기"))
struct VB_API FVBEffect_SetFact : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story", meta=(Categories="Story"))
	FGameplayTag Fact;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story")
	bool bOn = true;

	virtual void Apply(const FVBRuleContext& Context) const override;
};

/** 카운터에 더한다(음수면 뺀다). */
USTRUCT(BlueprintType, meta=(DisplayName="카운터 더하기"))
struct VB_API FVBEffect_AddCounter : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story", meta=(Categories="Story"))
	FGameplayTag Counter;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story")
	int32 Delta = 1;

	virtual void Apply(const FVBRuleContext& Context) const override;
};

/** 카운터를 그 값으로 정한다. 같은 값이면 바뀜도 알림도 없다(전력 배분 같은 값 상태용). */
USTRUCT(BlueprintType, meta=(DisplayName="카운터 정하기"))
struct VB_API FVBEffect_SetCounter : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story", meta=(Categories="Story"))
	FGameplayTag Counter;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story")
	int32 Value = 0;

	virtual void Apply(const FVBRuleContext& Context) const override;
};

// GE 걸기 효과도 Player/VBPlayerRules 에 있다(같은 이유).

/** 조건, 효과 배열을 평가, 적용하는 단 하나의 길. 대화, 퀘스트, 상점이 모두 이것을 부른다. */
namespace VBRules
{
	// 비교 하나. 카운터, 평판, 아이템 개수 조건이 같이 쓴다(셋째 쓰임에서 뽑았다).
	VB_API bool Compare(EVBRuleCompare Op, double Lhs, double Rhs);

	// 전부 참이면 참. 빈 배열 = 참. 바탕형이 아닌 원소(다른 구조체가 들어간 것)는 거짓으로 친다 - 알 수 없는 조건을 참으로 흘리지 않는다.
	VB_API bool EvaluateAll(const TArray<FInstancedStruct>& Conditions, const FVBRuleContext& Context);

	// 차례대로 적용한다. 효과를 적용할 수 없는 문맥(클라)이면 아무것도 안 바꾸고 경고한다. 반환 = 적용했는가.
	VB_API bool ApplyAll(const TArray<FInstancedStruct>& Effects, const FVBRuleContext& Context);

	enum class EApplyResult : uint8 { Landed, Failed, Interrupted };
	// 효과마다 PrepareNext 가 문맥을 채우고 계속할지 답한다(거짓 = Interrupted). 실패나 효과 아닌 원소면 Failed 로 멈춘다.
	// 배열은 매번 다시 읽는다 - 효과가 부르는 쪽을 끝낼 수 있다.
	VB_API EApplyResult ApplyAllUntilFailure(const TArray<FInstancedStruct>& Effects,
		TFunctionRef<bool(FVBRuleContext&)> PrepareNext);

#if WITH_EDITOR
	// 자산 검증용: 칸마다 Base 의 파생 구조체가 들었나. 빈 칸, 엉뚱한 구조체는 오류로 적는다(Where = 자리 이름).
	VB_API bool ValidateStructArray(const TArray<FInstancedStruct>& Items, const UScriptStruct* Base, const FText& Where,
		class FDataValidationContext& Context);
#endif
}
