#pragma once

#include "CoreMinimal.h"
#include "IChooserColumn.h"
#include "Animation/VBGASPCMCSelectionTypes.h"
#include "VBGASPActiveDatabaseColumn.generated.h"

class UPoseSearchDatabase;
class UChooserTable;

/** Pose Match 질의 한 번 전에 기존 무기 Chooser 의 database 세트를 빠짐없이 남긴다. */
USTRUCT(DisplayName="VB Active Pose Databases", meta=(Category="Filter"))
struct VB_API FVBGASPActiveDatabaseColumn : public FChooserColumnBase
{

	GENERATED_BODY()
public:
	/** FVBGASPCMCWeaponQuery 만 문맥으로 받는 독립 PSD 결과 chooser. */
	UPROPERTY(EditAnywhere, Category="Data")
	TObjectPtr<UChooserTable> CandidateChooser;
	UPROPERTY(EditAnywhere, Category="Data")
	TArray<TObjectPtr<UPoseSearchDatabase>> SourceDatabases;
	/** IsAnimationAssetLooping 으로 저작한 값, source/result 줄마다 하나. */
	UPROPERTY(EditAnywhere, Category="Data")
	TArray<bool> LoopingRows;

	virtual void Filter(FChooserEvaluationContext& Context, const FChooserIndexArray& In,
		FChooserIndexArray& Out) const override;
	virtual bool HasOutputs() const override { return true; }
	virtual void SetOutputs(FChooserEvaluationContext& Context, int RowIndex) const override;

#if WITH_EDITOR
	virtual bool HasPrimaryInput() override { return false; }
	virtual FName RowValuesPropertyName() override { return GET_MEMBER_NAME_CHECKED(FVBGASPActiveDatabaseColumn, SourceDatabases); }
	virtual void SetNumRows(int32 NumRows) override { SourceDatabases.SetNum(NumRows); LoopingRows.SetNum(NumRows); }
	virtual void InsertRows(int32 Index, int32 Count) override;
	virtual void DeleteRows(TArrayView<int> RowIndices) override;
	virtual void MoveRow(int32 SourceIndex, int32 TargetIndex) override;
	virtual void CopyRow(FChooserColumnBase& SourceColumn, int32 SourceIndex, int32 TargetIndex) override;
	virtual void ReplaceReferences(UObject* ReferenceToReplace, UObject* ReplaceWith) override;
#endif
};

/** 재생 metadata 만 낸다. 호출자의 후보 세트나 pose-search 결과는 덮지 않는다. */
USTRUCT(DisplayName="VB CMC Playback", meta=(Category="Output"))
struct VB_API FVBGASPCMCMetadataColumn : public FChooserColumnBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="Data")
	TArray<FVBGASPCMCChooserOutput> RowValues;
	UPROPERTY(EditAnywhere, Category="Data")
	FVBGASPCMCChooserOutput FallbackValue;
	virtual bool HasFilters() const override { return false; }
	virtual bool HasOutputs() const override { return true; }
	virtual void SetOutputs(FChooserEvaluationContext& Context, int RowIndex) const override;
#if WITH_EDITOR
	virtual bool HasPrimaryInput() override { return false; }
	virtual FName RowValuesPropertyName() override { return GET_MEMBER_NAME_CHECKED(FVBGASPCMCMetadataColumn, RowValues); }
	virtual void SetNumRows(int32 Count) override { RowValues.SetNum(Count); }
	virtual void InsertRows(int32 Index, int32 Count) override;
	virtual void DeleteRows(TArrayView<int> RowIndices) override;
	virtual void MoveRow(int32 SourceIndex, int32 TargetIndex) override;
	virtual void CopyRow(FChooserColumnBase& SourceColumn, int32 SourceIndex, int32 TargetIndex) override;
	virtual void CopyFallback(FChooserColumnBase& SourceColumn) override;
#endif
};
