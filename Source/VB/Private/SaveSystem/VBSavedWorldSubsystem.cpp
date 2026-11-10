// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "SaveSystem/VBSavedWorldSubsystem.h"

#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "Subsystems/SubsystemCollection.h"

void UVBSavedWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// 월드 서브시스템끼리 초기화 순서가 없어서 등록부를 먼저 세운다.
	Collection.InitializeDependency<UVBSaveParticipantSubsystem>();
	UVBSaveParticipantSubsystem::Register(this);
}

void UVBSavedWorldSubsystem::Deinitialize()
{
	UVBSaveParticipantSubsystem::Unregister(this);
	Super::Deinitialize();
}
