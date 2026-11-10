#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PoseSearch/PoseSearchHistory.h"
#include "Animation/VBGASPCMCSelectionTypes.h"
#include "VBGASPLocomotionStateLibrary.generated.h"

class UAnimInstance;
class UChooserTable;

// 이동 상태 그래프가 부르는 네이티브 함수 모음. 저작은 그래프와 Chooser 자산에 남고, 정의는 주제별 cpp 로 나뉜다.
UCLASS(meta=(BlueprintThreadSafe))
class VB_API UVBGASPLocomotionStateLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// CMC 선택기 평가 (VBGASPLocomotionStateLibrary_Evaluate.cpp)

	// 세 문맥을 받는 선택기를 평가한다. Pose history 는 언제나 LinkedInstance 의 것이다.
	static bool EvaluateCMCAnimation(UAnimInstance* LinkedInstance, UAnimInstance* ContextInstance,
		UChooserTable* Chooser, UAnimationAsset*& Animation, FVBGASPCMCChooserOutput& Output);
	// 선택 결과가 재생 입력으로 받아들일 만한가.
	static bool IsValidCMCSelection(const UAnimationAsset* Animation, const FVBGASPCMCChooserOutput& Output);
	// 포즈 기록에 표본이 둘 이상 쌓였나.
	static bool IsCMCPoseHistoryReady(UAnimInstance* Instance);

	// CMC 선택 갱신과 재생 입력 (VBGASPLocomotionStateLibrary_Selection.cpp)

	// 매 갱신 재선택이 필요한지 보고 필요하면 고른다.
	UFUNCTION(BlueprintCallable, Category="VB|GASP")
	static void UpdateCMCSelection(UAnimInstance* Instance);
	// 대기 중인 강제 블렌드 요청을 블렌드 스택에 전달한다.
	UFUNCTION(BlueprintCallable, Category="VB|GASP")
	static void ConsumeCMCForceBlend(UAnimInstance* Instance);
	// 그래프의 CMCBlendInputs 사본. 없으면 기본값.
	UFUNCTION(BlueprintPure, Category="VB|GASP")
	static FVBGASPCMCBlendInputs GetCMCBlendInputs(UAnimInstance* Instance);
	// 강제 요청 번호를 소비한다. 순수 번호 연산이다.
	static bool ConsumeCMCForceRequest(FVBGASPCMCState& State);

	// 상태 없는 규칙 (VBGASPLocomotionStateLibrary_Rules.cpp)

	// 재생 단계는 enum 순번이 아니라 저작한 상태 이름이 정한다.
	static EVBGASPCMCPhase GetCMCPhase(FName AuthoredState);
	// 선택에 쓸 단계. 하강 중인 공중 상태는 Loop 로 본다.
	static EVBGASPCMCPhase GetCMCSelectionPhase(FName AuthoredState, float VerticalSpeed);
	// 상태 이름과 단계에 맞춰 무기 질의의 판정 값을 고친다.
	static void ApplyCMCWeaponPhase(FVBGASPCMCWeaponQuery& Query, FName AuthoredState, EVBGASPCMCPhase Phase);
	// 재생 역할 태그를 DB 태그 이름으로 바꿔 DB 태그에 더한다.
	static TArray<FName> ProjectCMCPlaybackTags(const TArray<FName>& PlaybackTags, const TArray<FName>& DatabaseTags);
	// 전이 클립을 Loop 로 전환해도 되는가.
	static bool ShouldReleaseCMCTransition(EVBGASPCMCPhase Phase, bool bLooping,
		bool bToLoopNotify, float TimeRemaining, float BlendTime);
	// 재생 중인 시작, 멈춤 클립을 고를 때의 걸음과 지금 걸음이 다른가. 반복 클립은 상태 그래프가 맡는다.
	static bool ShouldReselectCMCForGait(EVBGASPCMCPhase Phase, bool bLooping, EVBGait SelectedGait, EVBGait WantedGait);
	// 선택기가 읽는 걸음. WeaponQuery.Gait 는 메인 갱신 전에 찍은 사본이라 걸음이 바뀐 프레임에 한 프레임 늦다.
	static EVBGait GetCMCSelectionGait(const FVBGASPCMCState& State);
	// 피벗 대체 래치의 한 걸음. 막힌 판정(IsPivoting)으로 풀면 래치가 자기 자신을 붙들므로 원판정으로 푼다.
	static bool StepPivotFallbackLatch(bool bLatched, bool bFellBackToLoop, bool bRawPivoting, bool bStanceChanged);
	// 지금 자세와 피벗 원판정을 읽어 래치를 한 번 갱신한다.
	static void RefreshPivotFallbackLatch(UAnimInstance* Instance, FVBGASPCMCState& State);
	// 래치를 소유한 기준(경로 무장/맨손, 무기 풀)이 바뀌었나.
	static bool HasPivotFallbackOwnerChanged(const FVBGASPCMCState& State);

	// 상태 그래프 진입점 (VBGASPLocomotionStateLibrary.cpp)

	// 네이티브 선택기가 있으면 그쪽으로, 없으면 옛 BlendStackInputs 길로 고른다.
	UFUNCTION(BlueprintCallable, Category="VB|GASP")
	static void SetBlendStackAnimFromChooser(UAnimInstance* Instance, uint8 StateMachineState, bool ForceBlend, UChooserTable* Chooser);
	UFUNCTION(BlueprintCallable, Category="VB|GASP")
	static void OnStateEntry_IdleLoop(UAnimInstance* Instance, UChooserTable* Chooser);
	UFUNCTION(BlueprintCallable, Category="VB|GASP")
	static void OnStateEntry_TransitionToIdle(UAnimInstance* Instance, UChooserTable* Chooser);
	UFUNCTION(BlueprintCallable, Category="VB|GASP")
	static void OnStateEntry_LocomotionLoop(UAnimInstance* Instance, UChooserTable* Chooser);
	UFUNCTION(BlueprintCallable, Category="VB|GASP")
	static void OnStateEntry_TransitionToLocomotion(UAnimInstance* Instance, UChooserTable* Chooser);
	UFUNCTION(BlueprintCallable, Category="VB|GASP")
	static void OnStateEntry_InAirLoop(UAnimInstance* Instance, UChooserTable* Chooser);
	UFUNCTION(BlueprintCallable, Category="VB|GASP")
	static void OnStateEntry_TransitionToInAir(UAnimInstance* Instance, UChooserTable* Chooser);
	UFUNCTION(BlueprintCallable, Category="VB|GASP")
	static void OnStateEntry_IdleBreak(UAnimInstance* Instance, UChooserTable* Chooser);
	UFUNCTION(BlueprintCallable, Category="VB|GASP")
	static void OnStateEntry_SlideLoop(UAnimInstance* Instance, UChooserTable* Chooser);
	UFUNCTION(BlueprintCallable, Category="VB|GASP")
	static void OnStateEntry_TransitionToSlide(UAnimInstance* Instance, UChooserTable* Chooser);
	UFUNCTION(BlueprintCallable, Category="VB|GASP")
	static void OnUpdate_TransitionToLocomotion(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="VB|GASP")
	static bool IsAnimationAlmostComplete(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="VB|GASP")
	static bool PlayLand(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="VB|GASP")
	static bool PlayMovingLand(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="VB|GASP")
	static FPoseHistoryReference Get_PoseHistoryReference(UAnimInstance* Instance);
};
