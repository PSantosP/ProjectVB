#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Character/VBWeaponManifest.h"
#include "Data/VBWeaponVisualConfig.h"
#include "VBSummonedWeapon.generated.h"

class USkeletalMeshComponent;

class UStaticMeshComponent;
class UNiagaraComponent;
class UMaterialInstanceDynamic;

// 로컬 코스메틱. 데미지/충돌/무기 상태의 소유자가 아니다.
UCLASS()
class VB_API AVBSummonedWeapon : public AActor
{
	GENERATED_BODY()
public:
	AVBSummonedWeapon();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB 소환 무기") FVBWeaponVisualPart VisualPart;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB 소환 무기") TObjectPtr<UStaticMeshComponent> WeaponMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB 소환 무기") TObjectPtr<UNiagaraComponent> FormationFX;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB 소환 무기") TObjectPtr<UNiagaraComponent> IdleFX;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="VB 소환 무기") float ManifestProgress = 0.0f;

	UFUNCTION(BlueprintCallable, CallInEditor, Category="VB 소환 무기") void RebuildVisual();
	// 메시 없는 부품이 모양을 가져올 보이는 메시. 바뀌면 이펙트에 다시 연결한다(외형 교체).
	void SetShapeBody(USkeletalMeshComponent* Body);
	UFUNCTION(BlueprintCallable, Category="VB 소환 무기") void SetManifestProgress(float Progress);
	UFUNCTION(BlueprintCallable, CallInEditor, Category="VB 소환 무기") void PreviewSummon();
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

private:
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ManifestMaterial;
	VBWeaponManifest::FRevealRange Reveal;
	float PreviewElapsed = -1.0f;
private:
	void BindShape();
	TWeakObjectPtr<USkeletalMeshComponent> ShapeBody;
};
