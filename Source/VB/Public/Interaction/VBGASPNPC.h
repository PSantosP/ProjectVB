#pragma once
#include "CoreMinimal.h"
#include "AIController.h"
#include "Character/VBCharacter.h"
#include "VBGASPNPC.generated.h"
class UStateTree;
class UStateTreeAIComponent;
class UVBLookAtInterestComponent;

/** 옮겨 온 GASP SmartObject/patrol StateTree 를 돌리는 우리 NPC. */
UCLASS()
class VB_API AVBGASPNPCController : public AAIController
{
	GENERATED_BODY()
public:
	AVBGASPNPCController();
	UPROPERTY(EditDefaultsOnly, Category="GASP") TObjectPtr<UStateTree> BehaviorStateTree;
	UPROPERTY(EditDefaultsOnly, Category="GASP") float InitialDelay = 2.f;
	UPROPERTY(EditDefaultsOnly, Category="GASP") float DedicatedServerInitialDelay = 8.f;
protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStateTreeAIComponent> StateTree;
	FTimerHandle StartTimer;
	void StartBehavior();
};

UCLASS()
class VB_API AVBGASPNPC : public AVBCharacter
{
	GENERATED_BODY()
public:
	AVBGASPNPC(const FObjectInitializer& ObjectInitializer);
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="NPC|Look At") TObjectPtr<UVBLookAtInterestComponent> LookAtInterest;
	/** 서 있는 미리보기나 행동을 밖에서 맡는 NPC 에서만 끈다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="NPC|Behavior") bool bEnableAmbientBehavior = true;
	/** 켜면 가까운 플레이어를 쳐다본다. 카메라를 잠그거나 AI controller 를 돌리지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="NPC|Look At") bool bLookAtNearbyPlayers = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="NPC|Look At", meta=(ClampMin="0", Units="cm")) float LookAtAcquireDistance = 400.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="NPC|Look At", meta=(ClampMin="0", Units="cm")) float LookAtReleaseDistance = 500.f;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	void UpdatePlayerLookAt();
	UFUNCTION() void OnRep_LookAtPlayer();
	UPROPERTY(ReplicatedUsing=OnRep_LookAtPlayer) TObjectPtr<APawn> LookAtPlayer;
	FTimerHandle LookAtTimer;
};
