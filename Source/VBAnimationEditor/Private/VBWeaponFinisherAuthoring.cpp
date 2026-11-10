#include "VBWeaponFinisherAuthoring.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/VBAnimNotify_InteractionRagdoll.h"
#include "Animation/VBAnimNotifyState_InteractionWarp.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "ContextualAnimSceneAsset.h"
#include "Data/VBFinisherConfig.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchIndex.h"
#include "PoseSearch/PoseSearchInteractionAsset.h"
#include "PoseSearch/PoseSearchNormalizationSet.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "RootMotionModifier_SkewWarp.h"
#include "UObject/Package.h"

// 에디터 모듈은 게임 모듈의 LogVB 를 못 쓴다(내보내지 않는다). 이름은 파일마다 달라야 unity 묶음에서 겹치지 않는다
//   (같은 모듈의 VBFinisherCameraAuthoringSession.cpp 가 LogVBFinisherAuthoring 을 쓴다).
DEFINE_LOG_CATEGORY_STATIC(LogVBWeaponFinisherAuthoring, Log, All);

namespace
{
	const FString GeneratedRoot(TEXT("/Game/VB/Animations/MotionMatching/Finisher/Weapons/"));
	const FName AttackerRole(TEXT("Attacker"));
	const FName VictimRole(TEXT("Victim"));

	template <typename T> T* FindOrCreateAsset(const FString& PackagePath)
	{
		if (UObject* Existing = LoadObject<UObject>(nullptr, *PackagePath))
		{
			return Cast<T>(Existing);
		}

		UPackage* Package = CreatePackage(*PackagePath);
		T* Asset = NewObject<T>(Package, *FPackageName::GetLongPackageAssetName(PackagePath), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Asset);
		return Asset;
	}

	template <typename T> T* DuplicateGenerated(T* Source, const FString& Directory, const FString& Name, bool* bOutReused = nullptr)
	{
		const FString Path = Directory / Name;
		if (bOutReused)
		{
			*bOutReused = false;
		}

		if (UObject* Existing = LoadObject<UObject>(nullptr, *Path))
		{
			if (bOutReused)
			{
				*bOutReused = true;
			}

			return Cast<T>(Existing);
		}

		IAssetTools& Assets = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		return Cast<T>(Assets.DuplicateAsset(Name, Directory, Source));
	}

	const FAnimSegment* GetSingleSegment(const UAnimMontage* Montage)
	{
		if (!Montage || Montage->SlotAnimTracks.Num() != 1 || Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() != 1)
		{
			return nullptr;
		}

		const FAnimSegment& Segment = Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0];
		// 시간 재매핑, 여러 section 장면은 조용히 펴지 않고 일부러 거절한다
		if (!FMath::IsNearlyZero(Segment.StartPos) || !FMath::IsNearlyZero(Segment.AnimStartTime) ||
			!FMath::IsNearlyEqual(Segment.AnimPlayRate, 1.0f) || Segment.LoopingCount != 1 ||
			!FMath::IsNearlyEqual(Montage->RateScale, 1.0f) || Montage->CompositeSections.Num() > 1)
		{
			return nullptr;
		}

		return &Segment;
	}

	void AddWarp(UAnimMontage* Montage, float EndTime)
	{
		UVBAnimNotifyState_InteractionWarp* Notify = NewObject<UVBAnimNotifyState_InteractionWarp>(Montage, NAME_None, RF_Transactional);
		Notify->AlignmentAnchorRole = AttackerRole;
		URootMotionModifier_SkewWarp* Modifier = NewObject<URootMotionModifier_SkewWarp>(Notify, NAME_None, RF_Transactional);
		Modifier->WarpTargetName = TEXT("VBWeaponInteraction");
		Modifier->bWarpTranslation = true;
		Modifier->bIgnoreZAxis = true;
		Modifier->bWarpRotation = true;
		Notify->RootMotionModifier = Modifier;
		FAnimNotifyEvent& Event = Montage->Notifies.AddDefaulted_GetRef();
		Event.NotifyName = TEXT("VBWeaponInteractionWarp");
		Event.NotifyStateClass = Notify;
		Event.Guid = FGuid::NewGuid();
		Event.Link(Montage, 0.0f);
		Event.TriggerTimeOffset = GetTriggerTimeOffsetForType(Montage->CalculateOffsetForNotify(0.0f));
		Event.SetDuration(EndTime);
		Event.EndLink.Link(Montage, EndTime);
	}

	void AddRelease(UAnimMontage* Montage, float Time)
	{
		UVBAnimNotify_InteractionRagdoll* Notify = NewObject<UVBAnimNotify_InteractionRagdoll>(Montage, NAME_None, RF_Transactional);
		FAnimNotifyEvent& Event = Montage->Notifies.AddDefaulted_GetRef();
		Event.NotifyName = TEXT("VBWeaponInteractionRelease");
		Event.Notify = Notify;
		Event.Guid = FGuid::NewGuid();
		Event.MontageTickType = EMontageNotifyTickType::BranchingPoint;
		Event.Link(Montage, Time);
		Event.TriggerTimeOffset = GetTriggerTimeOffsetForType(Montage->CalculateOffsetForNotify(Time));
	}
}

FString UVBWeaponFinisherAuthoring::BuildWeaponFinisherAssets(UVBFinisherConfig* Config, UPoseSearchSchema* Schema,
	const FString& DestinationPath)
{
	if (!Config || !Config->Scene)
	{
		return TEXT("Invalid config or missing scene");
	}

	const UContextualAnimSceneAsset* Scene = Config->Scene;
	const FContextualAnimSceneSection* Section = Scene->GetSection(0);
	if (Scene->GetNumSections() != 1 || !Section || Section->GetNumAnimSets() != 1 || Scene->GetNumRoles() != 2 ||
		!Scene->GetAttachmentParams().IsEmpty())
	{
		return TEXT("Expected one two-role scene without attachments");
	}

	const FContextualAnimTrack* AttackerTrack = Scene->GetAnimTrack(0, 0, AttackerRole);
	const FContextualAnimTrack* VictimTrack = Scene->GetAnimTrack(0, 0, VictimRole);
	if (!AttackerTrack || !VictimTrack)
	{
		return TEXT("Scene lacks Attacker/Victim tracks");
	}

	return BuildFromSources(Config, Schema, Cast<UAnimMontage>(AttackerTrack->Animation), Cast<UAnimMontage>(VictimTrack->Animation),
		AttackerTrack->MeshToScene, VictimTrack->MeshToScene, DestinationPath);
}

FString UVBWeaponFinisherAuthoring::BuildWeaponFinisherAssetsFromMontages(UVBFinisherConfig* Config, UPoseSearchSchema* Schema,
	UAnimMontage* AttackerMontage, UAnimMontage* VictimMontage, const FTransform& VictimOrigin, const FString& DestinationPath)
{
	// 짝 원점이 클립에 없는 팩(둘 다 root (0,0) yaw 0)은 장면 자산 없이 피해자 원점만 재서 넘긴다 - 공격자 원점은 항상 항등이다.
	return BuildFromSources(Config, Schema, AttackerMontage, VictimMontage, FTransform::Identity, VictimOrigin, DestinationPath);
}

FString UVBWeaponFinisherAuthoring::BuildFromSources(UVBFinisherConfig* Config, UPoseSearchSchema* Schema, UAnimMontage* AttackerMontage,
	UAnimMontage* VictimMontage, const FTransform& AttackerOrigin, const FTransform& VictimOrigin, const FString& DestinationPath)
{
	if (!Config || !Schema || !DestinationPath.StartsWith(GeneratedRoot) ||
		!FPackageName::IsValidLongPackageName(DestinationPath))
	{
		return TEXT("Invalid config/schema or destination outside generated weapon folder");
	}

	const FName Roles[] = {AttackerRole, VictimRole};
	const FTransform Origins[] = {AttackerOrigin, VictimOrigin};
	UAnimMontage* Sources[2] = {AttackerMontage, VictimMontage};
	UAnimSequence* Sequences[2];
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FAnimSegment* Segment = GetSingleSegment(Sources[Index]);
		Sequences[Index] = Segment ? Cast<UAnimSequence>(Segment->GetAnimReference()) : nullptr;
		if (!Sequences[Index] || !FMath::IsNearlyEqual(Segment->AnimEndTime, Sequences[Index]->GetPlayLength()) ||
			!FMath::IsNearlyEqual(Sequences[Index]->RateScale, 1.0f) ||
			Schema->GetSkeleton(Roles[Index]) != Sources[Index]->GetSkeleton() ||
			Sources[Index]->GetSkeleton() != Sequences[Index]->GetSkeleton())
		{
			return FString::Printf(TEXT("Unsupported montage layout or role skeleton: %s"), *Roles[Index].ToString());
		}
		// 지금 무기 짝 여덟은 notify 가 없다. 새 콘텐츠는 여기서 지우지 말고 자기 정책을 저작해야 한다
		if (!Sources[Index]->Notifies.IsEmpty() || !Sequences[Index]->Notifies.IsEmpty())
		{
			return TEXT("Source contains notifies; explicit conversion is required");
		}

		if (!Origins[Index].GetScale3D().Equals(FVector::OneVector) || Origins[Index].ContainsNaN())
		{
			return TEXT("Invalid origin transform");
		}
	}

	const IAnimationDataModel* VictimData = Sequences[1]->GetDataModel();
	if (!VictimData)
	{
		return TEXT("Victim source animation has no editable data model");
	}

	const float Frame = VictimData->GetFrameRate().AsInterval();
	const float ReleaseTime = Sources[1]->GetPlayLength() - Frame;
	const float WarpEnd = Config->AlignBlendTime;
	if (!FMath::IsFinite(Frame) || Frame <= 0.0f || WarpEnd <= Frame || ReleaseTime <= WarpEnd ||
		ReleaseTime >= Sources[0]->GetPlayLength())
	{
		return TEXT("Invalid alignment/release timing");
	}

	// 재생이 끝나기 전 마지막 저작 샘플을 쓴다 - GASP 시각이 아니라 무기 콘텐츠에 맞춘 값이다
	// 피해자 자동 blend 를 꺼서, 풀어 줄 때 idle blend 가 아니라 저작된 자세를 받게 한다
	UAnimMontage* Montages[2];
	for (int32 Index = 0; Index < 2; ++Index)
	{
		bool bSequenceReused = false;
		UAnimSequence* Sequence = DuplicateGenerated(Sequences[Index], DestinationPath, TEXT("AS_") + Roles[Index].ToString(),
			&bSequenceReused);
		Montages[Index] = DuplicateGenerated(Sources[Index], DestinationPath, TEXT("AM_") + Roles[Index].ToString());
		if (!Sequence || !Montages[Index])
		{
			return TEXT("Could not duplicate generated animation assets");
		}

		Sequence->Modify();
		// 이미 있는 AS 를 그대로 쓰면 원료를 고쳐도 옛 자세가 남으니, 다시 지을 때마다 원료의 애니 데이터를 덮어 쓴다
		if (bSequenceReused)
		{
			Sequence->GetController().PopulateWithExistingModel(Sequences[Index]->GetDataModelInterface());
		}

		Sequence->bEnableRootMotion = true;
		Sequence->RootMotionRootLock = ERootMotionRootLock::AnimFirstFrame;
		Sequence->PostEditChange();
		Sequence->MarkPackageDirty();
		UAnimMontage* Montage = Montages[Index];
		Montage->Modify();
		Montage->SlotAnimTracks = Sources[Index]->SlotAnimTracks;
		Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].SetAnimReference(Sequence);
		// UE 5.8 복제는 slot track 보다 composite model 을 먼저 채울 수 있고, PostEditChange 는 교체 뒤 캐시 길이를 다시 재지 않는다
		Montage->SetCompositeLength(Montage->CalculateSequenceLength());
		// 이 도구가 만드는 노티(정렬 워프, 피해자 풀어 주기)만 지우고 다시 단다
		// 손이나 스크립트로 단 다른 노티(타격 창 VBANS_AttackTraceWindow 등)는 다시 지어도 남긴다
		const int32 Before = Montage->Notifies.Num();
		Montage->Notifies.RemoveAll([](const FAnimNotifyEvent& Event)
		{
			return Cast<UVBAnimNotifyState_InteractionWarp>(Event.NotifyStateClass) != nullptr ||
				Cast<UVBAnimNotify_InteractionRagdoll>(Event.Notify) != nullptr;
		});
		if (Montage->Notifies.Num() > 0)
		{
			UE_LOG(LogVBWeaponFinisherAuthoring, Display, TEXT("%s: kept %d authored notifies (removed %d generated)"),
				*Montage->GetName(), Montage->Notifies.Num(), Before - Montage->Notifies.Num());
		}

		if (Montage->AnimNotifyTracks.IsEmpty())
		{
			Montage->InitializeNotifyTrack();
		}

		AddWarp(Montage, WarpEnd);
		if (Index == 1)
		{
			Montage->bEnableAutoBlendOut = false;
			AddRelease(Montage, ReleaseTime);
		}

		Montage->RefreshCacheData();
		Montage->PostEditChange();
		Montage->MarkPackageDirty();
	}

	UPoseSearchInteractionAsset* Interaction = FindOrCreateAsset<UPoseSearchInteractionAsset>(DestinationPath / TEXT("PSIA_Interaction"));
	UPoseSearchDatabase* Database = FindOrCreateAsset<UPoseSearchDatabase>(DestinationPath / TEXT("PSD_Interaction"));
	if (!Interaction || !Database)
	{
		return TEXT("Generated package has incompatible asset class");
	}

	Interaction->Modify();
	Interaction->Items.Reset();
	// 짝 클립을 단단히 맞춰 두고, 이동은 공격자에 고정한다
	Interaction->MinimalTranslationWeight = 0.0f;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FPoseSearchInteractionAssetItem& Item = Interaction->Items.AddDefaulted_GetRef();
		Item.Role = Roles[Index];
		Item.Animation = Montages[Index];
		Item.Origin = Origins[Index];
		// 위치만 고정한다. GASP 처럼 회전 가중치를 0 으로 두면 엔진이 짝의 위치로 방향을 낸다
		// UE 5.8 에서 회전 가중치 하나만 양수면 FindReferenceOrientation 이 가중 기준점을 하나도 못 얻는다
		Item.WarpingWeightTranslation = Index == 0 ? 1.0f : 0.0f;
		Item.WarpingWeightRotation = 0.0f;
	}

	Interaction->PostEditChange();
	Interaction->MarkPackageDirty();
	Database->Modify();
	Database->Schema = Schema;
	// 짝 클립마다 첫 자세 하나만 넣는다 - 샘플 하나면 PCA 분산이 없고, 이 크기에선 전수 탐색이 정확하고 싸다
	Database->PoseSearchMode = EPoseSearchMode::BruteForce;
	Database->ExcludeFromDatabaseParameters = FFloatInterval(0.0f, 0.0f);
	while (Database->GetNumAnimationAssets() > 0)
	{
		Database->RemoveAnimationAssetAt(Database->GetNumAnimationAssets() - 1);
	}

	FPoseSearchDatabaseAnimationAsset Entry;
	Entry.AnimAsset = Interaction;
	// 무기 짝 클립엔 저작된 건너뛰기/접근 구간이 없으니, 정렬이 끝나기 전 첫 샘플만 찾는다
	Entry.SamplingRange = FFloatInterval(0.0f, FMath::Min(Frame * 0.5f, WarpEnd * 0.5f));
	Entry.MirrorOption = EPoseSearchMirrorOption::UnmirroredOnly;
	Database->AddAnimationAsset(Entry);
	Database->PostEditChange();
	Database->MarkPackageDirty();
	UE_LOG(LogVBWeaponFinisherAuthoring, Display, TEXT("Weapon MMI authored %s: align %.4fs release %.4fs/%0.4fs, victim origin %s"),
		*Config->GetName(), WarpEnd, ReleaseTime, Sources[1]->GetPlayLength(), *Origins[1].ToHumanReadableString());
	return FString();
}

bool UVBWeaponFinisherAuthoring::NormalizeWeaponDatabases(const TArray<UPoseSearchDatabase*>& Databases, const FString& PackagePath)
{
	if (Databases.IsEmpty() || !PackagePath.StartsWith(GeneratedRoot) || !FPackageName::IsValidLongPackageName(PackagePath))
	{
		return false;
	}

	for (const UPoseSearchDatabase* Database : Databases)
	{
		if (!Database || !Database->GetPathName().StartsWith(GeneratedRoot))
		{
			return false;
		}
	}

	UPoseSearchNormalizationSet* Set = FindOrCreateAsset<UPoseSearchNormalizationSet>(PackagePath);
	if (!Set)
	{
		return false;
	}

	Set->Modify();
	Set->Databases.Reset();
	for (UPoseSearchDatabase* Database : Databases)
	{
		Set->Databases.Add(Database);
		Database->Modify();
		Database->NormalizationSet = Set;
		Database->MarkPackageDirty();
	}

	Set->PostEditChange();
	Set->MarkPackageDirty();
	return true;
}

FString UVBWeaponFinisherAuthoring::CheckWeaponDatabase(UPoseSearchDatabase* Database, bool bStartBuild)
{
	if (!Database || !Database->Schema || Database->GetNumAnimationAssets() != 1)
	{
		return TEXT("ERROR: missing database/schema/pair");
	}

	const FPoseSearchDatabaseAnimationAsset* Entry = Database->GetDatabaseAnimationAsset(0);
	const UPoseSearchInteractionAsset* Pair = Entry ? Cast<UPoseSearchInteractionAsset>(Entry->GetAnimationAsset()) : nullptr;
	if (!Pair || Pair->Items.Num() != 2 || !Database->NormalizationSet)
	{
		return TEXT("ERROR: missing pair/normalization set");
	}

	float RotationWeightSum = 0.0f;
	float MaxRotationWeight = 0.0f;
	for (const FPoseSearchInteractionAssetItem& Item : Pair->Items)
	{
		if (!FMath::IsFinite(Item.WarpingWeightRotation) || Item.WarpingWeightRotation < 0.0f || Item.WarpingWeightRotation > 1.0f)
		{
			return TEXT("ERROR: invalid rotation weight");
		}

		RotationWeightSum += Item.WarpingWeightRotation;
		MaxRotationWeight = FMath::Max(MaxRotationWeight, Item.WarpingWeightRotation);
	}
	// 가중치가 모두 0 이면 엔진의 균일 fallback 을 쓰므로 유효하다
	// 아니면 방향 기준이 가중치가 가장 큰 role 을 뺀다
	if (Pair->MinimalTranslationWeight < 1.0f && RotationWeightSum > UE_KINDA_SMALL_NUMBER &&
		(RotationWeightSum - MaxRotationWeight) / RotationWeightSum <= UE_SMALL_NUMBER)
	{
		return TEXT("ERROR: rotation reference has zero weight after excluding the anchor role");
	}

	for (const FName Role : {AttackerRole, VictimRole})
	{
		const UAnimMontage* Montage = Cast<UAnimMontage>(Pair->GetAnimationAsset(Role));
		if (!Montage || !Montage->HasRootMotion() || Database->Schema->GetSkeleton(Role) != Montage->GetSkeleton())
		{
			return TEXT("ERROR: role skeleton/root motion mismatch");
		}

		const FAnimNotifyEvent* Warp = Montage->Notifies.FindByPredicate([](const FAnimNotifyEvent& Event)
		{
			return Cast<UVBAnimNotifyState_InteractionWarp>(Event.NotifyStateClass) != nullptr;
		});
		if (!Warp || Warp->GetEndTriggerTime() <= Entry->SamplingRange.Max)
		{
			return TEXT("ERROR: search can start after alignment ends");
		}

		if (CastChecked<UVBAnimNotifyState_InteractionWarp>(Warp->NotifyStateClass)->AlignmentAnchorRole != AttackerRole)
		{
			return TEXT("ERROR: weapon alignment must preserve the Attacker origin");
		}

		if (Role == VictimRole)
		{
			const FAnimNotifyEvent* Release = Montage->Notifies.FindByPredicate([](const FAnimNotifyEvent& Event)
			{
				return Cast<UVBAnimNotify_InteractionRagdoll>(Event.Notify) != nullptr;
			});
			if (!Release || Montage->bEnableAutoBlendOut || Release->GetTriggerTime() <= Warp->GetEndTriggerTime() ||
				Release->GetTriggerTime() >= Montage->GetPlayLength())
			{
				return TEXT("ERROR: invalid victim release window");
			}
		}
	}

	using namespace UE::PoseSearch;
	const EAsyncBuildIndexResult Status = FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database,
		bStartBuild ? ERequestAsyncBuildFlag::NewRequest : ERequestAsyncBuildFlag::ContinueRequest);
	if (Status == EAsyncBuildIndexResult::InProgress)
	{
		return TEXT("PENDING");
	}

	if (Status != EAsyncBuildIndexResult::Success)
	{
		return TEXT("ERROR: index build failed");
	}

	const int32 Poses = Database->GetSearchIndex().GetNumPoses();
	return Poses > 0 ? FString::Printf(TEXT("OK: %d poses"), Poses) : TEXT("ERROR: empty search index");
}
