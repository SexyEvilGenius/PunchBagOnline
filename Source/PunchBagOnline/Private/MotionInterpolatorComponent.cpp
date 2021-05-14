#include "MotionInterpolatorComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Math/UnrealMath.h"

bool FMotionSnapshot::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	// pack bitfield with flags
	EMotionSnapshotFlags flags = EMotionSnapshotFlags::None;
	if (!Velocity.IsZero())
	{
		flags |= EMotionSnapshotFlags::HasVelocity;
	}
	if (!AngularVelocity.IsZero())
	{
		flags |= EMotionSnapshotFlags::HasAngularVelocity;
	}
	uint8 bits = static_cast<uint8>(flags);
	Ar.SerializeBits(&(bits), static_cast<uint8>(EMotionSnapshotFlags::FLAGS_COUNT));
	flags = static_cast<EMotionSnapshotFlags>(bits);

	bOutSuccess = true;
	bool bOutSuccessLocal = true;

	// update location, rotation, linear velocity
	bOutSuccess &= SerializePackedVector<10, 27>(Location, Ar);

	Rotation.NetSerialize(Ar, Map, bOutSuccessLocal);
	bOutSuccess &= bOutSuccessLocal;

	if (EnumHasAnyFlags(flags, EMotionSnapshotFlags::HasVelocity))
	{
		bOutSuccess &= SerializePackedVector<10, 27>(Velocity, Ar);
	}
	if (EnumHasAnyFlags(flags, EMotionSnapshotFlags::HasAngularVelocity))
	{
		bOutSuccess &= SerializePackedVector<10, 27>(AngularVelocity, Ar);
	}

	Ar << Timestamp;

	return true;
}

UMotionInterpolatorComponent::UMotionInterpolatorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	bAutoActivate = true;
	SetIsReplicatedByDefault(true);
}

void UMotionInterpolatorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	const float currentSyncedTime = GetWorld()->GetGameState()->GetServerWorldTimeSeconds();

	USceneComponent* component = GetComponentToSync();

	if (IsValid(component))
	{
		bool hasMovementAuthority = false;
		if (currentSyncedTime <= AuthorityReleaseTime)
		{
			hasMovementAuthority = true;
		}
		else
		{
			const AActor* Owner = GetOwner();
			if (IsValid(Owner))
			{
				hasMovementAuthority = GetOwnerRole() == ROLE_Authority ? !Owner->HasNetOwner() || Owner->HasLocalNetOwner() : Owner->HasLocalNetOwner();
			}
		}
		if (hasMovementAuthority)
		{
			Snapshots.Empty();
			if ((SyncPeriod < KINDA_SMALL_NUMBER || (currentSyncedTime - LastSyncTime) > SyncPeriod))
			{
				ServerSendSnapshot(FMotionSnapshot(*component, currentSyncedTime), GUID);
				LastSyncTime = currentSyncedTime;
			}
		}
		else if (SnapPeriod < KINDA_SMALL_NUMBER || (currentSyncedTime - LastSnapTime) > SnapPeriod)
		{
			FMotionSnapshot Snapshot;
			int OffBorder = 0;
			GetSnapshotAtTime(currentSyncedTime - NetworkDelay, UseExtrapolation, Snapshot, OffBorder);

			if (OffBorder == 0)
			{
				Snapshot.ApplyTo(*component);
				LastSnapTime = currentSyncedTime;
			}
		}
	}
}

void UMotionInterpolatorComponent::SetComponentOverride(class USceneComponent* InComponentOverride)
{
	ComponentOverride = InComponentOverride;
}

void UMotionInterpolatorComponent::AddSnapshot(const FMotionSnapshot& Snapshot)
{
	if (Snapshots.Num() >= BufferSize)
	{
		Snapshots.RemoveAt(0, (Snapshots.Num() - BufferSize) + 1);
	}
	Snapshots.Add(Snapshot);
	OnSnapshotAdded.Broadcast(Snapshot);
}

void UMotionInterpolatorComponent::GetSnapshotAtTime(float TargetTime, bool CanExtrapolate, FMotionSnapshot& Result, int& OffBorder)
{
	OffBorder = 0;
	if (Snapshots.Num() <= 0)
	{
		OffBorder = -1;
		return;
	}
	else if (TargetTime <= Snapshots[0].Timestamp)
	{
		OffBorder = -1;
		Result = Snapshots[0];
		return;
	}
	else if (TargetTime >= Snapshots.Last().Timestamp)
	{
		if (!CanExtrapolate)
		{
			OffBorder = 1;
			Result = Snapshots.Last();
			return;
		}
	}

	FMotionSnapshot FirstSnapshot;
	FMotionSnapshot SecondSnapshot;
	for (size_t i = 0; i < Snapshots.Num(); i++)
	{
		if (Snapshots[i].Timestamp == TargetTime)
		{
			Result = Snapshots[i];
			return;
		}
		else if (i + 1 < Snapshots.Num() && Snapshots[i + 1].Timestamp > TargetTime)
		{
			FirstSnapshot = Snapshots[i];
			SecondSnapshot = Snapshots[i + 1];
			Result = Interpolate(FirstSnapshot, SecondSnapshot, TargetTime);
			return;
		}
		else if (i + 1 == Snapshots.Num())
		{
			FirstSnapshot = Snapshots[i];
			Result = Extrapolate(FirstSnapshot, TargetTime);
			return;
		}
	}
}

void UMotionInterpolatorComponent::ServerSendSnapshot_Implementation(const FMotionSnapshot& InSnapshot, FGuid SenderGuid)
{
	MulticastSendSnapshot(InSnapshot, SenderGuid);
}

void UMotionInterpolatorComponent::MulticastSendSnapshot_Implementation(const FMotionSnapshot& InSnapshot, FGuid SenderGuid)
{
	if (SenderGuid != GUID)
	{
		AddSnapshot(InSnapshot);
	}
}

void UMotionInterpolatorComponent::OvertakeMovementAuthority(float Duration, float BlendOutDuration)
{
	AuthorityReleaseTime = GetWorld()->GetGameState()->GetServerWorldTimeSeconds() + Duration;
}

FMotionSnapshot UMotionInterpolatorComponent::Interpolate(const FMotionSnapshot& FirstSnapshot, const FMotionSnapshot& SecondSnapshot, float TargetTime)
{
	FMotionSnapshot Result;

	float Alpha = UKismetMathLibrary::NormalizeToRange(TargetTime, FirstSnapshot.Timestamp, SecondSnapshot.Timestamp);

	float PredictionTime = TargetTime - FirstSnapshot.Timestamp;
	float ReversePredictionTime = SecondSnapshot.Timestamp - TargetTime;

	// Location of the object predicted from what we knew before
	FVector ForwardPrediction = FirstSnapshot.Location + (FirstSnapshot.Velocity * PredictionTime);
	// Location of the object calculated from what we know now
	FVector BackwardPrediction = SecondSnapshot.Location + (SecondSnapshot.Velocity * ReversePredictionTime * -1);

	Result.Location = FMath::Lerp(ForwardPrediction, BackwardPrediction, Alpha);

	// Non-linear interpolation for rotation. No backward prediction required.
	Result.Rotation = FMath::Lerp(FirstSnapshot.Rotation, SecondSnapshot.Rotation, FMath::InterpSinInOut<float>(0.f, 1.f, Alpha));

	Result.Velocity = FMath::Lerp(FirstSnapshot.Velocity, SecondSnapshot.Velocity, Alpha);
	Result.AngularVelocity = FMath::Lerp(FirstSnapshot.AngularVelocity, SecondSnapshot.AngularVelocity, Alpha);

	Result.Timestamp = TargetTime;

	return Result;
}

FMotionSnapshot UMotionInterpolatorComponent::Extrapolate(const FMotionSnapshot& Snapshot, float TargetTime)
{
	FMotionSnapshot Result = Snapshot;

	float PredictionTime = TargetTime - Snapshot.Timestamp;

	Result.Location = Snapshot.Location + (Snapshot.Velocity * PredictionTime);
	Result.Rotation = (FRotator::MakeFromEuler(Snapshot.AngularVelocity) * PredictionTime).Quaternion() * Snapshot.Rotation;

	Result.Timestamp = TargetTime;
	return Result;
}

USceneComponent* UMotionInterpolatorComponent::GetComponentToSync()
{
	if (IsValid(ComponentOverride))
	{
		return ComponentOverride;
	}
	if (!IsValid(ComponentToSync) || ComponentToSync->GetFName() != SyncedComponentName)
	{
		if (SyncedComponentName == NAME_None)
		{
			ComponentToSync = GetOwner()->GetRootComponent();
		}
		else
		{
			for (UActorComponent* Comp : GetOwner()->GetComponents())
			{
				if (Comp->GetFName() == SyncedComponentName)
				{
					if (UChildActorComponent* ChildActorComp = Cast<UChildActorComponent>(Comp))
					{
						if (AActor* ChildActor = ChildActorComp->GetChildActor())
						{
							ComponentToSync = ChildActor->GetRootComponent();
						}
					}
					else
					{
						ComponentToSync = Cast<USceneComponent>(Comp);
					}
					break;
				}
			}
		}
	}
	return ComponentToSync;
}