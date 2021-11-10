#include "MotionInterpolatorComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Math/UnrealMath.h"

FMotionSnapshot::FMotionSnapshot(const USceneComponent& InComponent, float InTimestamp) :
	Location(InComponent.GetComponentLocation()),
	Rotation(InComponent.GetComponentRotation()),
	Velocity(InComponent.GetComponentVelocity()),
	Timestamp(InTimestamp)
{
	const UPrimitiveComponent* asPrimitiveComponent = Cast<const UPrimitiveComponent>(&InComponent);
	if (IsValid(asPrimitiveComponent))
	{
		AngularVelocity = asPrimitiveComponent->GetPhysicsAngularVelocityInDegrees();
	}
}

FMotionSnapshot::FMotionSnapshot(const USceneComponent& InComponent) :
	FMotionSnapshot(InComponent, 0.0f)
{
	const UWorld* World = InComponent.GetWorld();
	if (IsValid(World))
	{
		Timestamp = World->GetGameState()->GetServerWorldTimeSeconds();
	}
}

void FMotionSnapshot::ApplyTo(USceneComponent& InComponent)
{
	InComponent.SetWorldLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	UPrimitiveComponent* asPrimitiveComponent = Cast<UPrimitiveComponent>(&InComponent);
	if (IsValid(asPrimitiveComponent))
	{
		asPrimitiveComponent->SetPhysicsLinearVelocity(Velocity);
		asPrimitiveComponent->SetPhysicsAngularVelocityInDegrees(AngularVelocity);
	}
}

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

	// update location, rotation, linear velocity
	bOutSuccess &= SerializePackedVector<10, 27>(Location, Ar);

	Rotation.SerializeCompressedShort(Ar);

	if (EnumHasAnyFlags(flags, EMotionSnapshotFlags::HasVelocity))
	{
		bOutSuccess &= SerializePackedVector<10, 27>(Velocity, Ar);
	}
	if (EnumHasAnyFlags(flags, EMotionSnapshotFlags::HasAngularVelocity))
	{
		bOutSuccess &= SerializePackedVector<10, 27>(AngularVelocity, Ar);
	}

	Ar << Timestamp;

	if (Ar.IsLoading())
	{
		const UWorld* World = Map->GetWorld();
		if (IsValid(World) && IsValid(World->GetGameState()))
		{
			ArrivalTime = World->GetGameState()->GetServerWorldTimeSeconds();
		}
	}

	return true;
}

UMotionInterpolatorComponent::UMotionInterpolatorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	bAutoActivate = true;
	SetIsReplicatedByDefault(true);
}

void UMotionInterpolatorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	const float currentSyncedTime = GetGameState()->GetServerWorldTimeSeconds();

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

		if (HadMovementAuthority != hasMovementAuthority)
		{
			if (!HadMovementAuthority)
			{
				Snapshots.Empty();
			}
			else
			{
				CurrentAuthorityBlendTime = AuthorityBlendTime;
			}
			HadMovementAuthority = hasMovementAuthority;
		}

		if (hasMovementAuthority)
		{
			float syncPeriod = SyncPeriod;
			if (CurrentHightFreqSyncDuration > KINDA_SMALL_NUMBER || CurrentHightFreqSyncDuration == -1.0f)
			{
				CurrentHightFreqSyncDuration = FMath::Max(CurrentHightFreqSyncDuration-DeltaTime, 0.0f);
				syncPeriod = HighFreqSyncPeriod;
			}
			if ((syncPeriod < KINDA_SMALL_NUMBER || (currentSyncedTime - LastSyncTime) > syncPeriod))
			{
				ServerSendSnapshot(FMotionSnapshot(*component, currentSyncedTime), GUID);
				LastSyncTime = currentSyncedTime;
			}
		}
		else if (SnapPeriod < KINDA_SMALL_NUMBER || (currentSyncedTime - LastSnapTime) > SnapPeriod)
		{
			FMotionSnapshot Snapshot;
			int OffBorder = 0;
			GetSnapshotAtTime(GetLookupTime(), UseExtrapolation, Snapshot, OffBorder);

			if (OffBorder == 0)
			{
				if (CurrentAuthorityBlendTime > KINDA_SMALL_NUMBER)
				{
					CurrentAuthorityBlendTime -= DeltaTime;
					const float Alpha = 1 - (CurrentAuthorityBlendTime / AuthorityBlendTime);
					Snapshot = SimpleInterpolate(FMotionSnapshot(*component), Snapshot, Alpha);
					Snapshot.Velocity = FVector::ZeroVector;
					Snapshot.AngularVelocity = FVector::ZeroVector;
				}
				Snapshot.ApplyTo(*component);
				LastSnapTime = currentSyncedTime;
			}
			else
			{
				OnNotEnoughData.Broadcast();
			}
		}
	}

	if (!bUseFixedNetworkDelay)
	{
		NetworkDelay = FMath::FInterpTo(NetworkDelay, TargetNetworkDelay, DeltaTime, NetworkDelayInterpolationSpeed);
	}

	if (GetOwnerRole() == ROLE_Authority && CurrentOwnershipDuration > KINDA_SMALL_NUMBER)
	{
		CurrentOwnershipDuration -= DeltaTime;
		if (CurrentOwnershipDuration < KINDA_SMALL_NUMBER)
		{
			ClientReleaseOwnership();
		}
	}

	if (CurrentAdditionalNetworkDelay != TargetAdditionalNetworkDelay)
	{
		CurrentAdditionalNetworkDelay = FMath::FInterpTo(CurrentAdditionalNetworkDelay, TargetAdditionalNetworkDelay, DeltaTime, NetworkDelayInterpolationSpeed);
		if (CurrentAdditionalNetworkDelay == TargetAdditionalNetworkDelay)
		{
			OnAdditionalDelayReached.ExecuteIfBound();
			OnAdditionalDelayReached.Unbind();
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
		if (!bUseFixedNetworkDelay)
		{
			float TimeSinceLastSnapshot = 0.0f;
			const AGameStateBase* gameState = GetGameState();
			if (IsValid(gameState) && Snapshots.Num() > 1)
			{
				TimeSinceLastSnapshot = gameState->GetServerWorldTimeSeconds() - Snapshots[Snapshots.Num()-2].ArrivalTime;
			}
			const float maxDelay = GetSnapshotsMaxDelay();
			TargetNetworkDelay = (maxDelay+TimeSinceLastSnapshot)*1.5;
		}
	}
}

void UMotionInterpolatorComponent::ClientSendSnapshot_Implementation(const FMotionSnapshot& InSnapshot)
{
	AddSnapshot(InSnapshot);
}

void UMotionInterpolatorComponent::ServerTakeOwnership_Implementation(AActor* newOwner, float OwnershipDuration)
{
	ensureMsgf(GetOwnerRole() == ENetRole::ROLE_Authority, TEXT("'%s' calls ServerTakeOwnership on client! This will have no effect."), *GetName());
	AActor* componentOwner = GetOwner();
	if (IsValid(componentOwner) && (!componentOwner->HasNetOwner() || newOwner != componentOwner->GetOwner()))
	{
		componentOwner->SetOwner(newOwner);
		CurrentOwnershipDuration = OwnershipDuration;
	}
}

void UMotionInterpolatorComponent::ServerReleaseOwnership_Implementation(const FMotionSnapshot& InSnapshot, float ClientNetworkDelay)
{
	TargetAdditionalNetworkDelay = -(NetworkDelay + ClientNetworkDelay);
	OnAdditionalDelayReached.ExecuteIfBound();
	OnAdditionalDelayReached.Unbind();

	OnAdditionalDelayReached.BindLambda([this]() {
		AActor* componentOwner = GetOwner();
		if (IsValid(componentOwner))
		{
			componentOwner->SetOwner(nullptr);
		}
		TargetAdditionalNetworkDelay = 0.0f;
		EnableTempHighFreqUpdate();
	});
}

void UMotionInterpolatorComponent::EnableTempHighFreqUpdate()
{
	CurrentHightFreqSyncDuration = HighFreqSyncDuration;
}

void UMotionInterpolatorComponent::SetHighFreqUpdateEnabled(bool Enabled)
{
	CurrentHightFreqSyncDuration = Enabled ? -1.0f : 0.0f;
}

void UMotionInterpolatorComponent::ClientReleaseOwnership_Implementation()
{
	const FMotionSnapshot& latestSnapshot = FMotionSnapshot(*GetComponentToSync());
	ServerReleaseOwnership(latestSnapshot, NetworkDelay);
	EnableTempHighFreqUpdate();
	//TargetAdditionalNetworkDelay = NetworkDelay;
}

float UMotionInterpolatorComponent::GetLookupTime()
{
	const AGameStateBase* gameState = GetGameState();
	if (IsValid(gameState))
	{
		return gameState->GetServerWorldTimeSeconds() - GetLookupTimeOffset();
	}
	return 0.0f;
}

void UMotionInterpolatorComponent::OvertakeMovementAuthority(float Duration)
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

FMotionSnapshot UMotionInterpolatorComponent::SimpleInterpolate(const FMotionSnapshot& FirstSnapshot, const FMotionSnapshot& SecondSnapshot, float Alpha)
{
	FMotionSnapshot Result;
	Result.Location =FMath::Lerp(FirstSnapshot.Location, SecondSnapshot.Location, Alpha);
	Result.Rotation =FMath::Lerp(FirstSnapshot.Rotation, SecondSnapshot.Rotation, FMath::InterpSinInOut<float>(0.f, 1.f, Alpha));
	Result.Velocity = FMath::Lerp(FirstSnapshot.Velocity, SecondSnapshot.Velocity, Alpha);
	Result.AngularVelocity = FMath::Lerp(FirstSnapshot.AngularVelocity, SecondSnapshot.AngularVelocity, Alpha);
	Result.Timestamp = FMath::Lerp(FirstSnapshot.Timestamp, SecondSnapshot.Timestamp, Alpha);
	return Result;
}

FMotionSnapshot UMotionInterpolatorComponent::Extrapolate(const FMotionSnapshot& Snapshot, float TargetTime)
{
	FMotionSnapshot Result = Snapshot;

	float PredictionTime = TargetTime - Snapshot.Timestamp;

	Result.Location = Snapshot.Location + (Snapshot.Velocity * PredictionTime);
	Result.Rotation = Snapshot.Rotation + FRotator::MakeFromEuler(Snapshot.AngularVelocity * PredictionTime);

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

float UMotionInterpolatorComponent::GetSnapshotsMaxDelay()
{
	float MaxDelay = 0.0f;
	if (Snapshots.Num() > 1)
	{
		for (int32 i = 1; i < Snapshots.Num(); ++i)
		{
			const FMotionSnapshot& Snapshot = Snapshots[i];
			const float Delay = Snapshot.ArrivalTime - Snapshot.Timestamp;
			MaxDelay = FMath::Max(Delay, MaxDelay);
		}
	}
	return MaxDelay;
}

float UMotionInterpolatorComponent::GetLookupTimeOffset()
{
	return NetworkDelay + CurrentAdditionalNetworkDelay;
}

const AGameStateBase* UMotionInterpolatorComponent::GetGameState()
{
	if (!IsValid(CachedGameState))
	{
		const UWorld* World = GetWorld();
		if (IsValid(World) && IsValid(World->GetGameState()))
		{
			CachedGameState = World->GetGameState();
		}
	}
	return CachedGameState;
}
