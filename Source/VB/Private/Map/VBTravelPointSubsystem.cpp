// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Map/VBTravelPointSubsystem.h"

#include "Engine/World.h"
#include "Interaction/VBSavePoint.h"
#include "SaveSystem/VBSaveGame.h"
#include "VB/VB.h"

UVBTravelPointSubsystem* UVBTravelPointSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UVBTravelPointSubsystem>() : nullptr;
}

void UVBTravelPointSubsystem::WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context)
{
	Save.Progress.DiscoveredTravelPoints = Discovered;
}

void UVBTravelPointSubsystem::ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	// 빈 ID 와 겹친 ID 만 거른다. 이 맵에 배치되지 않은 ID 는 남긴다 - 버리면 다른 맵에서 발견한 지점이 다음 저장 때 사라진다.
	Discovered.Reset();
	for (const FGameplayTag& PointId : Save.Progress.DiscoveredTravelPoints)
	{
		if (!IsUsablePointId(PointId))
		{
			// 이름이 바뀌었거나 지워진 태그. 리다이렉트가 없으면 그 발견은 잃는다 - 조용히 버리지 않고 말한다.
			VB_LOG(Warning, "빠른 이동: 저장의 지점 ID %s 를 쓸 수 없다(빈 태그·뿌리 태그) - 버린다", *PointId.ToString());
			continue;
		}

		Discovered.AddUnique(PointId);
	}

	OnDiscoveredChanged.Broadcast();
}

void UVBTravelPointSubsystem::ApplyServerDiscovered(const TArray<FGameplayTag>& ServerDiscovered)
{
	// 서버가 이미 거른 목록이다. 그래도 같은 규칙으로 한 번 더 거른다 - 태그 표가 다른 빌드에서 빈 태그로 올 수 있다.
	Discovered.Reset();
	for (const FGameplayTag& PointId : ServerDiscovered)
	{
		if (!IsUsablePointId(PointId))
		{
			// 버리면 클라의 사본이 서버 목록과 달라진다 - ReadSave 처럼 말한다.
			VB_LOG(Warning, "빠른 이동: 서버가 보낸 지점 ID %s 를 쓸 수 없다(빈 태그·뿌리 태그) - 버린다", *PointId.ToString());
			continue;
		}

		Discovered.AddUnique(PointId);
	}

	OnDiscoveredChanged.Broadcast();
}

bool UVBTravelPointSubsystem::IsUsablePointId(const FGameplayTag& PointId)
{
	// 뿌리(Map.TravelPoint)만 고른 것은 "아직 정하지 않음" 이다 - 편집기의 범주 필터가 뿌리도 고르게 둔다.
	static const FGameplayTag Root = FGameplayTag::RequestGameplayTag(TEXT("Map.TravelPoint"), false);
	return PointId.IsValid() && PointId != Root;
}

bool UVBTravelPointSubsystem::Register(AVBSavePoint* Point)
{
	if (!Point)
	{
		return false;
	}

	const FGameplayTag PointId = Point->GetPointId();
	if (!IsUsablePointId(PointId))
	{
		// 지점 ID 가 없으면 목적지가 될 수 없다(어디로 가는지 저장에 적을 이름이 없다). 저장점 노릇은 그대로 한다.
		VB_LOG(Warning, "빠른 이동: %s 에 지점 ID(PointId) 가 없다 - 목적지로 등록하지 않는다", *Point->GetName());
		return false;
	}

	if (const AVBSavePoint* Existing = FindPoint(PointId); Existing && Existing != Point)
	{
		VB_LOG(Warning, "빠른 이동: %s 와 %s 가 같은 지점 ID %s 를 쓴다 - 뒤의 것을 등록하지 않는다",
			*Existing->GetName(), *Point->GetName(), *PointId.ToString());
		return false;
	}

	Points.AddUnique(Point);
	return true;
}

void UVBTravelPointSubsystem::Unregister(const AVBSavePoint* Point)
{
	Points.RemoveAll([Point](const TWeakObjectPtr<AVBSavePoint>& Entry) { return !Entry.IsValid() || Entry.Get() == Point; });
}

AVBSavePoint* UVBTravelPointSubsystem::FindPoint(const FGameplayTag& PointId) const
{
	if (!PointId.IsValid())
	{
		return nullptr;
	}

	for (const TWeakObjectPtr<AVBSavePoint>& Entry : Points)
	{
		AVBSavePoint* Point = Entry.Get();
		if (Point && Point->GetPointId() == PointId)
		{
			return Point;
		}
	}

	return nullptr;
}

TArray<AVBSavePoint*> UVBTravelPointSubsystem::GetPoints() const
{
	TArray<AVBSavePoint*> Live;
	for (const TWeakObjectPtr<AVBSavePoint>& Entry : Points)
	{
		if (AVBSavePoint* Point = Entry.Get())
		{
			Live.Add(Point);
		}
	}

	return Live;
}

bool UVBTravelPointSubsystem::Discover(const FGameplayTag& PointId)
{
	const UWorld* World = GetWorld();
	if (!IsUsablePointId(PointId) || !World || World->GetNetMode() == NM_Client)
	{
		return false;
	}

	if (Discovered.Contains(PointId))
	{
		return false;
	}

	Discovered.Add(PointId);
	VB_LOG(Log, "빠른 이동: 지점 발견 %s (발견 %d 곳)", *PointId.ToString(), Discovered.Num());
	OnDiscoveredChanged.Broadcast();
	return true;
}

bool UVBTravelPointSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// 지점은 플레이 중에만 등록된다(BeginPlay). 에디터 월드에 빈 등록부를 만들 까닭이 없다.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}
