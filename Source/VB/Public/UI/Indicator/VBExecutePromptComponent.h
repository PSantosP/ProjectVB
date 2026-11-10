// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "UI/Indicator/VBIndicatorTypes.h"
#include "VBExecutePromptComponent.generated.h"

class AController;
class APawn;
class UVBRegisteredIndicatorWidget;

/**
 * 처형 프롬프트 - 지금 처형 키를 누르면 걸릴 적 하나에 월드 표시 요청을 건다. 로컬 플레이어가 조종하는 동안만 타이머가 돈다.
 * 대상은 처형 능력과 같은 질의로 묻는다(UVBAbilitySystemStatics::FindExecuteTarget) - 프롬프트와 실제 처형이 갈리지 않게.
 * 원뿔 탐색이 들어 있어 매 틱이 아니라 짧은 타이머로 묻는다.
 */
UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBExecutePromptComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBExecutePromptComponent();

	// 대상을 다시 묻고 요청을 걸거나 바꾸거나 뗀다. 타이머가 부르고, 시험이 직접 부른다.
	void RefreshPrompt();

	// 지금 요청을 건 대상. 없으면 null.
	AActor* GetPromptTarget() const { return PromptTarget.Get(); }

	// 지금 건 요청의 손잡이(등록 안 됐으면 무효).
	const FVBIndicatorHandle& GetPromptHandle() const { return Registration.GetHandle(); }

	// 다시 묻는 타이머가 돌고 있는가(로컬 조종 중에만 참).
	bool IsPromptArmed() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 지금 처형 키가 걸 대상. 로컬 폰이 아니거나 처형 능력이 없으면 null.
	virtual AActor* FindPromptTarget(APawn& Pawn) const;

	// 프롬프트를 그릴 위젯 클래스(WBP_ExecutePrompt). 요청의 Data 로 실려 등록 표시기 갈래(UVBRegisteredIndicatorsWidget)가 만든다. 비면 시작할 때 한 번 경고한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TSubclassOf<UVBRegisteredIndicatorWidget> PromptWidgetClass;

	// 대상 루트에서 프롬프트를 띄울 월드 오프셋.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FVector WorldOffset = FVector(0.0f, 0.0f, 120.0f);

	// 대상을 다시 묻는 간격(초).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI", meta=(ClampMin="0.02"))
	float RefreshIntervalSeconds = 0.1f;

private:
	// 소유 폰의 조종자가 바뀌었다(빙의, 해제, 클라 복제). 로컬 조종이면 타이머를 켜고 아니면 끄고 요청을 뗀다.
	UFUNCTION()
	void HandleControllerChanged(APawn* Pawn, AController* OldController, AController* NewController);

	// 지금 로컬 조종 여부에 타이머를 맞춘다.
	void UpdateArming();

	// 요청을 떼고 대상을 비운다.
	void ClearPrompt();

	// 지금 요청이 걸린 대상.
	TWeakObjectPtr<AActor> PromptTarget;

	// 등록 하나. 떼는 길은 ClearPrompt 와 EndPlay 이고, 소멸자도 뗀다.
	FVBIndicatorRegistration Registration;

	// 다시 묻는 타이머.
	FTimerHandle RefreshTimer;
};
