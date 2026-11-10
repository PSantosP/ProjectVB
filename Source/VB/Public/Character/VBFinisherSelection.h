// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UPoseSearchDatabase;
class UVBFinisherConfig;
struct FPoseSearchBlueprintResult;
struct FVBInteractionReplay;

// 새 경로 시작을 묻는 한 번의 질문. 세션 사실은 부르는 쪽이 값으로 넣어 선택이 세션 클래스를 모르게 한다.
struct FVBFinisherStartQuery
{
	AActor* Owner = nullptr;
	AActor* Target = nullptr;
	bool bOwnerPlaying = false; // UVBFinisherComponent::IsPlaying() - 공격자가 이미 처형 장면 안이다
	bool bTargetInScene = false; // UVBFinisherComponent::IsActorInScene(Target) - 대상이 이미 어느 처형 안이다
};

/**
 * 처형 선택과 성립 검사를 담당한다(UVBFinisherComponent 가 값으로 가진다). 성공한 후보 하나를 돌려주고 게시는 하지 않는다.
 * 재생 자원, 세션 게시, 피해는 컴포넌트와 어빌리티의 몫이다. 상태는 WarnedEmptyWeapons 하나뿐이다.
 */
class VB_API FVBFinisherSelection final
{
public:
	// 새 경로의 자료가 있는가 - 경로를 가르는 유일한 질문이다. 자료가 있는데 성립하지 않으면 옛 경로로 가지 않고 피해 없이 끝난다.
	bool HasInteractionFinisherData(const AActor* Owner) const;
	// 새 경로를 지금 시작할 수 있는가(검색은 안 돌린다). 어빌리티가 자기 공격을 끊기 전에 묻는다.
	//   권한, 대상, 애님, 모션워핑, 이동 상태, 처형 DB 를 본다.
	bool CanStartInteractionFinisher(const FVBFinisherStartQuery& Query) const;
	// 옛 경로(ContextualAnim 장면)의 처형을 고른다: 현재 무기의 목록 중 접근 방향이 맞는 것에서 랜덤. 없으면 nullptr.
	const UVBFinisherConfig* SelectFinisher(const AActor* Owner, const AActor* Target) const;
	// 역할과 이동 공간이 맞는 짝을 고른다. 세션 게시는 재생 성공 뒤 컴포넌트가 맡는다.
	bool TrySelectInteraction(const FVBFinisherStartQuery& Query,
		TArray<FPoseSearchBlueprintResult>& OutResults, FVBInteractionReplay& OutChoice) const;
	// 등록된 무기 전부의 처형 DB 색인 빌드를 시작한다(에디터 전용 효과). 컴포넌트의 BeginPlay 가 부른다.
	void Prewarm(const AActor* Owner) const;

private:
	// 현재 무기의 처형 DB. Target 이 있으면 그 접근 방향에 맞는 것만. 무기 전용 목록이 비면 공격자 ABP 의 공용 chooser.
	void GatherInteractionDatabases(const AActor* Owner, TArray<UPoseSearchDatabase*>& OutDatabases, const AActor* Target = nullptr) const;
	const UVBFinisherConfig* FindInteractionConfig(const AActor* Owner, const UPoseSearchDatabase* Database) const;
	// 두 골격에 안 맞는 DB 를 뺀다(엔진은 막지 않고 크래시한다). 버린 DB 는 프로세스당 한 번 경고한다.
	static void FilterDatabasesBySkeleton(TArray<UPoseSearchDatabase*>& Databases, const AActor* Attacker, const AActor* Victim);

	// 빈 무기 목록 경고를 무기마다 한 번만 - 처형 입력은 연타로 들어온다. 이것이 이 객체의 유일한 상태다.
	mutable TSet<uint8> WarnedEmptyWeapons;
};
