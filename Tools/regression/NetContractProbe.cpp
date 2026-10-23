#include "../../Engine/Network/Contracts/NetWireIds.h"
#include "../../Engine/Network/Contracts/INetworkTransport.h"
#include "../../Engine/Network/Contracts/NetProtocolHeader.h"
#include "../../Engine/Network/Contracts/INetChannelPolicy.h"
#include "../../Engine/Network/Contracts/INetTelemetrySink.h"
#include "../../Engine/Network/Contracts/INetworkEntityRegistry.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
using namespace ce::net;
static int checks;
static void Check(bool value) { ++checks; if (!value) throw std::runtime_error("ND-a contract assertion failed"); }

// Contract consumers only. These small fixtures are not the N4 transport or N2
// production registry and are deliberately outside the frozen header directory.
class Transport final : public INetworkTransport
{
    struct Message { NetReceivedMessage metadata{}; std::array<std::byte,64> data{}; };
    std::array<Message,2> queue_{};
    std::uint32_t count_{};
    NetConnectionState state_{NetConnectionState::Disconnected};
    NetTransportStatistics statistics_{};
public:
    NetTransportResult Connect(NetPeerId peer, const NetEndpoint&) noexcept override
    { if (!peer.value) return NetTransportResult::InvalidArgument; state_=NetConnectionState::Connected; return NetTransportResult::Ok; }
    NetTransportResult Send(NetPeerId peer, NetLane lane, NetDelivery delivery, std::span<const std::byte> payload) noexcept override
    {
        if (state_ != NetConnectionState::Connected) return NetTransportResult::Disconnected;
        if (!peer.value || payload.size()>64) return NetTransportResult::InvalidArgument;
        if (count_ == queue_.size()) return NetTransportResult::Full;
        auto& message=queue_[count_++]; message.metadata={peer,lane,delivery,static_cast<std::uint32_t>(payload.size())};
        if (!payload.empty()) std::memcpy(message.data.data(),payload.data(),payload.size());
        ++statistics_.sent_messages; statistics_.sent_bytes+=payload.size();
        ++statistics_.pending_messages; statistics_.pending_bytes+=payload.size();
        return NetTransportResult::Ok;
    }
    NetTransportResult Receive(NetReceivedMessage& metadata,std::span<std::byte> output) noexcept override
    {
        metadata={}; if (!count_) return NetTransportResult::Empty;
        metadata=queue_[0].metadata;
        if(output.size()<metadata.bytes) return NetTransportResult::BufferTooSmall;
        if(metadata.bytes) std::memcpy(output.data(),queue_[0].data.data(),metadata.bytes);
        queue_[0]=queue_[1]; --count_;
        ++statistics_.received_messages; statistics_.received_bytes+=metadata.bytes;
        --statistics_.pending_messages; statistics_.pending_bytes-=metadata.bytes;
        return NetTransportResult::Ok;
    }
    void Pump() noexcept override { if(state_==NetConnectionState::Draining && !count_)state_=NetConnectionState::Closed; }
    void BeginDrain(NetPeerId) noexcept override { state_=NetConnectionState::Draining; }
    void Disconnect(NetPeerId) noexcept override { state_=NetConnectionState::Closed; count_=0;statistics_.pending_messages=statistics_.pending_bytes=0; }
    NetConnectionState State(NetPeerId) const noexcept override { return state_; }
    NetTransportStatistics Statistics() const noexcept override { return statistics_; }
};
class Registry final : public INetworkEntityRegistry
{
    NetEntityBinding binding_{};
    NetworkObjectId retired_{};
    bool Matches(const NetEntityBinding& expected) const noexcept
    { return expected.object.value==binding_.object.value && expected.entity.value==binding_.entity.value
        && expected.scene.value==binding_.scene.value && expected.revision==binding_.revision; }
public:
    NetRegistryResult Bind(NetworkObjectId id,NetEntityToken entity,NetSceneToken scene,NetEntityBinding& out) noexcept override
    {
        out={}; if(!id.value || !entity.value || !scene.value)return NetRegistryResult::InvalidArgument;
        if(id.value==retired_.value)return NetRegistryResult::Retired;
        if(binding_.object.value)return NetRegistryResult::AlreadyBound;
        binding_={id,entity,scene,1};out=binding_;return NetRegistryResult::Ok;
    }
    NetRegistryResult Rebind(const NetEntityBinding& expected,NetEntityToken entity,NetSceneToken scene,NetEntityBinding& out) noexcept override
    {
        out={};if(!binding_.object.value || !Matches(expected))return NetRegistryResult::StaleBinding;
        if(!entity.value || !scene.value)return NetRegistryResult::InvalidArgument;
        if(binding_.revision==(std::numeric_limits<std::uint64_t>::max)())return NetRegistryResult::Full;
        binding_.entity=entity;binding_.scene=scene;++binding_.revision;out=binding_;return NetRegistryResult::Ok;
    }
    NetRegistryResult Unbind(const NetEntityBinding& expected) noexcept override
    {
        if(!binding_.object.value || !Matches(expected))return NetRegistryResult::StaleBinding;
        retired_=binding_.object;binding_={};return NetRegistryResult::Ok;
    }
    NetRegistryResult Resolve(NetworkObjectId id,NetEntityBinding& out) const noexcept override
    { out={};if(!id.value)return NetRegistryResult::InvalidArgument;if(id.value!=binding_.object.value)return NetRegistryResult::NotFound;out=binding_;return NetRegistryResult::Ok; }
};
class Channels final : public INetChannelPolicy
{ public: NetChannelRule Select(NetMessageClass kind) const noexcept override { return DefaultChannelRule(kind); } };
class Telemetry final : public INetTelemetrySink
{
public:
    std::array<std::uint64_t,11> counts{};
    void Record(const NetTelemetryEvent& event) noexcept override
    {
        const auto index=static_cast<std::uint8_t>(event.kind);if(index>=counts.size())return;
        if(event.kind==NetTelemetryKind::Pending || event.kind==NetTelemetryKind::QueueDepth)counts[index]=event.value;
        else counts[index]+=event.value;
    }
};
int main() try
{
    static_assert(sizeof(NetworkObjectId)==8 && sizeof(NetComponentTypeId)==4 && sizeof(NetFieldId)==2);
    static_assert(!std::is_convertible_v<NetTick,NetworkObjectId> && std::is_trivially_copyable_v<NetProtocolHeader>);
    Check(contract_version==1 && protocol_header_bytes==36);
    NetProtocolHeader header{protocol_magic,protocol_version,{0x1234},0x0807060504030201ull,{10},{9},10};
    std::array<std::byte,38> message{};
    Check(EncodeHeader(header,message));message[36]=std::byte{0xaa};message[37]=std::byte{0x03};
    const std::array<unsigned char,36> golden{0x43,0x45,0x4e,0x54,1,0,0x34,0x12,1,2,3,4,5,6,7,8,10,0,0,0,0,0,0,0,9,0,0,0,0,0,0,0,10,0,0,0};
    for(std::size_t i=0;i<golden.size();++i) Check(std::to_integer<unsigned char>(message[i])==golden[i]);
    NetProtocolHeader decoded;std::span<const std::byte> payload;
    Check(DecodeMessage(message,header.compatibility,2,decoded,payload)==NetDecodeResult::Ok);
    Check(decoded.message.value==0x1234 && decoded.server_tick.value==10 && payload.size()==2);
    auto empty_header=header;empty_header.payload_bits=0;empty_header.baseline_tick={0};
    std::array<std::byte,36> empty_message{};
    Check(EncodeHeader(empty_header,empty_message));
    Check(DecodeMessage(empty_message,header.compatibility,0,decoded,payload)==NetDecodeResult::Ok && payload.empty());
    empty_header.compatibility=0;Check(EncodeHeader(empty_header,empty_message));
    Check(DecodeMessage(empty_message,0,0,decoded,payload)==NetDecodeResult::SchemaMismatch);
    for(std::size_t size=0;size<protocol_header_bytes;++size)
        Check(DecodeMessage(std::span(message).first(size),header.compatibility,2,decoded,payload)==NetDecodeResult::Truncated && payload.empty());
    auto invalid=message;invalid[0]=std::byte{};
    Check(DecodeMessage(invalid,header.compatibility,2,decoded,payload)==NetDecodeResult::BadMagic && !decoded.message.value);
    invalid=message;invalid[4]=std::byte{2};
    Check(DecodeMessage(invalid,header.compatibility,2,decoded,payload)==NetDecodeResult::VersionMismatch);
    Check(DecodeMessage(message,0,2,decoded,payload)==NetDecodeResult::SchemaMismatch);
    invalid=message;invalid[6]=invalid[7]=std::byte{};
    Check(DecodeMessage(invalid,header.compatibility,2,decoded,payload)==NetDecodeResult::InvalidMessage);
    auto future=header;future.baseline_tick={11};Check(EncodeHeader(future,invalid));
    Check(DecodeMessage(invalid,header.compatibility,2,decoded,payload)==NetDecodeResult::InvalidBaseline);
    Check(DecodeMessage(message,header.compatibility,1,decoded,payload)==NetDecodeResult::PayloadTooLarge);
    Check(DecodeMessage(std::span(message).first(37),header.compatibility,2,decoded,payload)==NetDecodeResult::SizeMismatch);
    invalid=message;invalid.back()=std::byte{0x80};
    Check(DecodeMessage(invalid,header.compatibility,2,decoded,payload)==NetDecodeResult::NonzeroPadding);
    std::array<std::byte,39> trailing{};std::memcpy(trailing.data(),message.data(),message.size());
    Check(DecodeMessage(trailing,header.compatibility,2,decoded,payload)==NetDecodeResult::SizeMismatch);
    auto maximal=header;maximal.payload_bits=0xffffffffu;Check(EncodeHeader(maximal,invalid));
    Check(DecodeMessage(invalid,header.compatibility,0xffffffffu,decoded,payload)==NetDecodeResult::SizeMismatch);
    std::array<std::byte,4> scalar{};NetWriter writer(scalar);
    Check(!writer.Write(1,0) && !writer.Write(1,9) && !writer.Write(256,1) && writer.Written()==0);
    Check(writer.Write(0x12345678,4) && !writer.Write(1,1) && writer.Written()==4);
    NetReader reader(scalar);std::uint64_t value=99;
    Check(!reader.Read(value,8) && value==99 && reader.ReadBytes()==0);
    Check(reader.Read(value,4) && value==0x12345678 && !reader.Read(value,1));
    auto untouched=scalar;Check(!EncodeHeader(header,scalar) && scalar==untouched);
    Transport transport;INetworkTransport& wire=transport;
    Check(wire.Connect({}, {})==NetTransportResult::InvalidArgument);
    Check(wire.Connect({1},{})==NetTransportResult::Ok);
    scalar[0]=std::byte{7};Check(wire.Send({1},NetLane::Control,NetDelivery::ReliableOrdered,scalar)==NetTransportResult::Ok);
    scalar[0]=std::byte{99};Check(wire.Send({1},NetLane::Input,NetDelivery::UnreliableSequenced,{})==NetTransportResult::Ok);
    Check(wire.Send({1},NetLane::State,NetDelivery::UnreliableSequenced,{})==NetTransportResult::Full);
    NetReceivedMessage metadata;Check(wire.Receive(metadata,{})==NetTransportResult::BufferTooSmall && metadata.bytes==4);
    wire.BeginDrain({1});Check(wire.Send({1},NetLane::Control,NetDelivery::ReliableOrdered,{})==NetTransportResult::Disconnected);
    wire.Pump();Check(wire.State({1})==NetConnectionState::Draining);
    Check(wire.Receive(metadata,scalar)==NetTransportResult::Ok && scalar[0]==std::byte{7} && metadata.lane==NetLane::Control);
    Check(wire.Receive(metadata,{})==NetTransportResult::Ok && metadata.bytes==0 && metadata.lane==NetLane::Input);
    wire.Pump();Check(wire.State({1})==NetConnectionState::Closed && wire.Statistics().pending_messages==0);
    Check(wire.Receive(metadata,scalar)==NetTransportResult::Empty && metadata.bytes==0);
    Registry registry;INetworkEntityRegistry& ids=registry;NetEntityBinding first,next,out;
    Check(ids.Bind({}, {1},{1},out)==NetRegistryResult::InvalidArgument);
    Check(ids.Bind({42},{101},{1},first)==NetRegistryResult::Ok);
    Check(ids.Bind({42},{101},{1},out)==NetRegistryResult::AlreadyBound);
    Check(ids.Rebind(first,{101},{2},next)==NetRegistryResult::Ok && next.object.value==42 && next.revision==2);
    Check(ids.Unbind(first)==NetRegistryResult::StaleBinding);
    Check(ids.Rebind(first,{202},{3},out)==NetRegistryResult::StaleBinding);
    Check(ids.Resolve({42},out)==NetRegistryResult::Ok && out.scene.value==2);
    Check(ids.Unbind(next)==NetRegistryResult::Ok);
    Check(ids.Resolve({42},out)==NetRegistryResult::NotFound && !out.entity.value);
    Check(ids.Bind({42},{202},{3},out)==NetRegistryResult::Retired);
    Channels channels;
    for(auto kind:{NetMessageClass::Handshake,NetMessageClass::Lifecycle,NetMessageClass::ConfirmedRpc})
        Check(channels.Select(kind).delivery==NetDelivery::ReliableOrdered && channels.Select(kind).lane==NetLane::Control);
    Check(channels.Select(NetMessageClass::Snapshot).delivery==NetDelivery::UnreliableSequenced);
    Check(channels.Select(NetMessageClass::Input).lane==NetLane::Input);
    Check(channels.Select(NetMessageClass::Chat).priority<channels.Select(NetMessageClass::Input).priority);
    Check(!channels.Select(NetMessageClass::AssetTransfer).allowed && !channels.Select(static_cast<NetMessageClass>(255)).allowed);
    Telemetry telemetry;INetTelemetrySink& sink=telemetry;
    for(auto kind:{NetTelemetryKind::Enqueued,NetTelemetryKind::Applied,NetTelemetryKind::Dropped,NetTelemetryKind::Invalid,NetTelemetryKind::Unauthorized})
    { sink.Record({{10},{1},2,0,kind});sink.Record({{10},{1},3,0,kind});Check(telemetry.counts[static_cast<int>(kind)]==5); }
    sink.Record({{10},{1},8,0,NetTelemetryKind::Pending});sink.Record({{11},{1},1,0,NetTelemetryKind::Pending});
    Check(telemetry.counts[static_cast<int>(NetTelemetryKind::Pending)]==1);
    std::printf("NET_CONTRACTS_OK checks=%d headerBytes=36 interfaces=4 transport=fixture registry=fixture\n",checks);
}
catch(const std::exception& error)
{
    std::fprintf(stderr,"NET_CONTRACTS_FAILED check=%d %s\n",checks,error.what());return 1;
}
