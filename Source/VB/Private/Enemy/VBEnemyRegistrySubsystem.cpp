// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Enemy/VBEnemyRegistrySubsystem.h"

#include "Enemy/VBEnemyBase.h"

void UVBEnemyRegistrySubsystem::Register(AVBEnemyBase* Enemy)
{
	if (Enemy && !Enemies.ContainsByPredicate([Enemy](const TWeakObjectPtr<AVBEnemyBase>& Entry) { return Entry.Get() == Enemy; }))
	{
		Enemies.Add(Enemy);
	}
}

void UVBEnemyRegistrySubsystem::Unregister(const AVBEnemyBase* Enemy)
{
	Enemies.RemoveAll([Enemy](const TWeakObjectPtr<AVBEnemyBase>& Entry) { return !Entry.IsValid() || Entry.Get() == Enemy; });
}

const TArray<TWeakObjectPtr<AVBEnemyBase>>& UVBEnemyRegistrySubsystem::GetEnemies()
{
	Enemies.RemoveAll([](const TWeakObjectPtr<AVBEnemyBase>& Entry) { return !Entry.IsValid(); });
	return Enemies;
}
