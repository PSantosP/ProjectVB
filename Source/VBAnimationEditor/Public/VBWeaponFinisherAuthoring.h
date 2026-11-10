#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBWeaponFinisherAuthoring.generated.h"

class UAnimMontage;
class UVBFinisherConfig;
class UPoseSearchSchema;
class UPoseSearchDatabase;

/** CAS의 저작된 역할/원점을 보존해 프로젝트 소유 MMI 데이터를 만든다. 저장/활성화는 호출자 책임이다. */
UCLASS()
class VBANIMATIONEDITOR_API UVBWeaponFinisherAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	// 성공은 빈 문자열. 실패 시 Config는 변경하지 않는다. 원본 몽타주/시퀀스도 변경하지 않는다.
	UFUNCTION(BlueprintCallable, Category="VB|Animation|Editor")
	static FString BuildWeaponFinisherAssets(UVBFinisherConfig* Config, UPoseSearchSchema* Schema, const FString& DestinationPath);

	// 장면 자산(CAS) 없이 역할 몽타주 둘과 피해자 원점으로 같은 자료를 만든다. 공격자 원점은 항등.
	// 왜 있나: CAS 트랙은 파이썬으로 못 고친다. 짝 원점이 클립에 없는 팩은 원점을 재서 여기로 넘긴다.
	//   Config->Scene 은 건드리지 않는다 - 그 처형은 옛 ContextualAnim 경로(SelectFinisher 가 Scene 을 요구)에는 안 뜨고 상호작용 경로로만 돈다.
	UFUNCTION(BlueprintCallable, Category="VB|Animation|Editor")
	static FString BuildWeaponFinisherAssetsFromMontages(UVBFinisherConfig* Config, UPoseSearchSchema* Schema,
		UAnimMontage* AttackerMontage,
		UAnimMontage* VictimMontage, const FTransform& VictimOrigin, const FString& DestinationPath);

	// 여러 후보 DB를 같은 척도로 비교하도록 정규화 집합을 공유한다.
	UFUNCTION(BlueprintCallable, Category="VB|Animation|Editor")
	static bool NormalizeWeaponDatabases(const TArray<UPoseSearchDatabase*>& Databases, const FString& PackagePath);

	// 비동기 인덱스만 요청/확인한다. 재생이나 PIE를 실행하지 않는다.
	UFUNCTION(BlueprintCallable, Category="VB|Animation|Editor")
	static FString CheckWeaponDatabase(UPoseSearchDatabase* Database, bool bStartBuild);

private:
	// 두 공개 함수의 공통 몸통. 원본 몽타주, 시퀀스는 바꾸지 않는다.
	static FString BuildFromSources(UVBFinisherConfig* Config, UPoseSearchSchema* Schema, UAnimMontage* AttackerMontage,
		UAnimMontage* VictimMontage, const FTransform& AttackerOrigin, const FTransform& VictimOrigin, const FString& DestinationPath);
};
