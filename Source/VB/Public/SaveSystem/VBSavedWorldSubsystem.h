// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "VBSavedWorldSubsystem.generated.h"

/**
 * 저장에 참여하는 월드 서브시스템의 공용 부모. 등록부 등록과 해제를 맡고, 자식은 자기 구역만 읽고 쓴다.
 */
UCLASS(Abstract)
class VB_API UVBSavedWorldSubsystem : public UWorldSubsystem, public IVBSaveParticipant
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override PURE_VIRTUAL(UVBSavedWorldSubsystem::WriteSave, );
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override PURE_VIRTUAL(UVBSavedWorldSubsystem::ReadSave, );
};
