// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "VBInteractableComponent.generated.h"

class UPrimitiveComponent;
class UVBInteractableConfig;
struct FVBRuleContext;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVBInteractedSignature, AActor*, InteractingActor);
// bInstant = 애니 없이 끝 자세로 맞춘다(세이브 로드). 평소 상호작용과 복제 도착은 false
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVBActivationChangedSignature, bool, bActivated, bool, bInstant);

/**
 * 상호작용 대상. 문, 레버 같은 월드 액터에 붙이고, 완료 후 할 일은 소유 액터가 OnInteracted 로 정한다.
 * 컴포넌트 위치가 거리, 시야 판정과 프롬프트의 앵커다. 완료 신호는 서버에서만 오고 켜짐 여부는 복제된다.
 */
UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBInteractableComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UVBInteractableComponent();

	// 미할당이면 CDO 를 돌려준다(null 이 아니다). 대신 BeginPlay 가 경고를 남긴다
	const UVBInteractableConfig* GetConfig() const;

	bool IsInteractionEnabled() const { return bInteractionEnabled; }

	// 같은 상호작용 대상의 추가 물리 조각을 읽는다.
	const TArray<TObjectPtr<AActor>>& GetTargetParts() const { return TargetParts; }

	// 서버에서만. 잠긴 문, 이미 쓴 일회용 대상 등에 쓴다
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="VB|Interaction")
	void SetInteractionEnabled(bool bEnabled);

	// 바닥, 탁자에 놓인 물건용. 이 지점을 메시 윗면 위로 띄워 시야 캡슐이 놓인 면에 박히지 않게 한다
	void PlaceAboveRestingMesh(const UPrimitiveComponent& RestingMesh);

	// 프롬프트에 나올 행동 이름. 덮어쓴 것이 있으면 그것, 없으면 설정의 ActionText
	FText GetActionText() const;

	// 상태에 따라 행동 이름을 바꾼다(열린 문 = 「닫기」). 빈 텍스트면 설정 값으로 돌아간다. 프롬프트가 클라에서 그려져 복제한다
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="VB|Interaction")
	void SetActionTextOverride(const FText& InText);

	// 서버 완료 처리의 단일 입구(UVBGA_Interact, 자동 대상은 인터랙터). UseConditions 가 거짓이면 아무것도 안 하고 false
	bool NotifyInteracted(AActor* InteractingActor);

	// OnInteracted 안에서 소유 액터가 동작 실패를 알린다(서버). 그러면 완료 사건, 효과, OnCompletionApplied 가 없다
	void ReportActionFailed(const FString& Reason);

	// 조건 칸 판정. 비면 true. 서버에서 부른다(클라는 자기 상태로만 판정한다)
	bool PassesUseConditions(const AActor* InteractingActor) const;

	// 시도가 조건에 걸려 거절됐을 때(UVBGA_Interact, 서버) 거절 효과를 적용한다. 화면 글은 GA 가 인터랙터로 보낸다
	void ApplyRefused(AActor* InteractingActor);

	const FText& GetRefusedText() const { return RefusedText; }
	// 완료 때 내는 것(효과, 이벤트 태그)이 있나. 입력만 받는 대상(전력 스위치)은 비어 있어야 거절된 누름이 보상을 내지 않는다
	bool HasCompletionOutputs() const { return !CompletionEffects.IsEmpty() || !EventTags.IsEmpty(); }

	// 완료. 서버에서만 온다
	UPROPERTY(BlueprintAssignable, Category="VB|Interaction")
	FVBInteractedSignature OnInteracted;

	// 완료 처리가 다 끝났다(서버). OnInteracted -> 완료 사건 -> 완료 효과 다음이다. 아이템 사건이 완료 사건보다 먼저 가면 안 되는 보상은 여기서 준다
	UPROPERTY(BlueprintAssignable, Category="VB|Interaction")
	FVBInteractedSignature OnCompletionApplied;

	// 켜짐 상태(문 열림, 스위치 켜짐)의 유일한 기준 값. 저장도 이 값을 읽고 쓴다
	bool IsActivated() const { return bActivated; }

	// 서버에서만. 바뀌면 OnActivationChanged 가 울린다. bInstant 는 세이브 로드용(애니 없이 끝 자세)이고 클라에는 안 간다(알려진 MP 구멍)
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="VB|Interaction")
	void SetActivated(bool bNewActivated, bool bInstant = false);

	// 세이브 키. 저장하지 않으면 NAME_None, 기본은 소유 액터 이름. 런타임 스폰 액터는 이름이 매번 달라 SaveId 를 적거나 저장을 끈다(SetSaveState)
	UFUNCTION(BlueprintPure, Category="VB|Interaction")
	FName GetSaveKey() const;

	// 이 대상의 이름표. SaveId, 비면 소유 액터 이름. bSaveState 와 무관하게 자리 저장 같은 다른 구역도 쓴다
	FName GetObjectKey() const;

	// 서로 다른 두 대상이 같은 저장 키(GetSaveKey)를 나눠 쓰는가. IsDataValid 와 등록 경고가 같이 쓴다
	static bool SharesSaveKey(const UVBInteractableComponent& A, const UVBInteractableComponent& B);

#if WITH_EDITOR
	// 같은 월드에서 SharesSaveKey 인 다른 대상이 있으면 오류. 불러온 액터만 보고, 안 불러온 월드 파티션 칸의 충돌은 등록 경고가 잡는다
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	// 자리 저장 참여 여부. 기본은 false 이고 움직이는 대상(UVBPushableComponent)만 덮어쓴다
	virtual bool IsTransformSaved() const { return false; }
	// 처음 놓인 자리에서 벗어났는가. 벗어났으면 Out 에 지금 자리를 준다(저장 맵에는 벗어난 대상만 남는다)
	virtual bool CaptureMovedTransform(FTransform& Out) const { return false; }
	// 저장된 자리로 옮긴다(서버). 지금은 옮길 수 없으면(누가 밀고 있음) false
	virtual bool ApplySavedTransform(const FTransform& Saved) { return false; }

	// 런타임 생성 대상(떨군 줍기)이 저장을 끈다. 등록 때 이 키를 쓰므로 BeginPlay 전(지연 스폰 중)에 부른다
	void SetSaveState(bool bInSaveState) { bSaveState = bInSaveState; }

	// 상태가 바뀔 때만 울린다. 컴포넌트 BeginPlay 가 소유 액터 구독보다 먼저라 시작 상태는 소유 액터가 IsActivated 로 읽는다
	UPROPERTY(BlueprintAssignable, Category="VB|Interaction")
	FVBActivationChangedSignature OnActivationChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Interaction")
	TObjectPtr<UVBInteractableConfig> Config;

	// 같은 대상의 물리 조각이다. 상태를 동기화하는 LinkedProps 와는 별개다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Interaction|Reach")
	TArray<TObjectPtr<AActor>> TargetParts;

	// 배치 시 꺼 둘 수 있다(잠긴 문으로 시작)
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category="VB|Interaction")
	bool bInteractionEnabled = true;

	// 배치 시 켜진 채(열린 문으로) 시작할 수 있다
	UPROPERTY(EditAnywhere, ReplicatedUsing=OnRep_Activated, BlueprintReadOnly, Category="VB|Interaction")
	bool bActivated = false;

	UFUNCTION()
	void OnRep_Activated();

	// 켜짐 상태를 세이브에 남기는가. 한 번 쓰고 마는 대상(저장 포인트 등)은 끈다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Interaction|Save")
	bool bSaveState = true;

	// 비어 있으면 소유 액터 이름이 키다(GetSaveKey)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Interaction|Save")
	FName SaveId;

	// 이 대상이 무엇인가(예: Interaction.Door.Ward). 비어 있지 않으면 완료 때 Event.Interaction.Completed 를 이 태그로 방송한다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Interaction|Quest", meta=(Categories="Interaction"))
	FGameplayTagContainer EventTags;

	// 완료 때 적용할 효과(서버, 위 사건 뒤). 부른 플레이어 = 상호작용한 폰의 PlayerState. 완료마다 돌므로 한 번만 줄 것은 AVBToggleProp::bOneShot 에 단다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Interaction|Quest",
		meta=(BaseStruct="/Script/VB.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> CompletionEffects;

	// 이 대상을 쓸 수 있는 조건(전부 참, 비면 언제나). 시작과 완료 둘 다 보며 도중에 거짓이 되면 완료 신호와 효과가 안 난다. 프롬프트는 안 바꾼다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Interaction|Quest",
		meta=(BaseStruct="/Script/VB.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> UseConditions;

	// 시도가 거절될 때 HUD 알림 줄에 띄울 한 줄. 비면 글 없이 거절한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Interaction|Quest")
	FText RefusedText;

	// 거절 때 서버 효과. 시도마다 돈다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Interaction|Quest",
		meta=(BaseStruct="/Script/VB.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> RefusedEffects;

private:
	// 조건, 효과의 문맥. 부른 플레이어 = 상호작용한 폰의 PlayerState, 상대 = 이 대상의 소유 액터
	FVBRuleContext MakeRuleContext(const AActor* InteractingActor) const;

	UPROPERTY(Replicated)
	FText ActionTextOverride;

	// 지금 완료 처리 중인 상호작용의 동작 실패 이유(서버, NotifyInteracted 동안만). 비면 성공
	FString ActionFailure;
	// 같은 실패를 한 번만 알리려고 마지막 이유를 기억한다
	FString LastReportedFailure;
};
