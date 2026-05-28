# ruff: noqa
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class GetPluginInfoRequest(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class GetPluginInfoResponse(_message.Message):
    __slots__ = ("name", "vendor_version", "manifest")
    class ManifestEntry(_message.Message):
        __slots__ = ("key", "value")
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: str
        def __init__(self, key: _Optional[str] = ..., value: _Optional[str] = ...) -> None: ...
    NAME_FIELD_NUMBER: _ClassVar[int]
    VENDOR_VERSION_FIELD_NUMBER: _ClassVar[int]
    MANIFEST_FIELD_NUMBER: _ClassVar[int]
    name: str
    vendor_version: str
    manifest: _containers.ScalarMap[str, str]
    def __init__(self, name: _Optional[str] = ..., vendor_version: _Optional[str] = ..., manifest: _Optional[_Mapping[str, str]] = ...) -> None: ...

class GetPluginCapabilitiesRequest(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class GetPluginCapabilitiesResponse(_message.Message):
    __slots__ = ("capabilities",)
    CAPABILITIES_FIELD_NUMBER: _ClassVar[int]
    capabilities: _containers.RepeatedCompositeFieldContainer[PluginCapability]
    def __init__(self, capabilities: _Optional[_Iterable[_Union[PluginCapability, _Mapping]]] = ...) -> None: ...

class PluginCapability(_message.Message):
    __slots__ = ("service", "volume_expansion")
    class Service(_message.Message):
        __slots__ = ("type",)
        class Type(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            UNKNOWN: _ClassVar[PluginCapability.Service.Type]
            CONTROLLER_SERVICE: _ClassVar[PluginCapability.Service.Type]
            VOLUME_ACCESSIBILITY_CONSTRAINTS: _ClassVar[PluginCapability.Service.Type]
        UNKNOWN: PluginCapability.Service.Type
        CONTROLLER_SERVICE: PluginCapability.Service.Type
        VOLUME_ACCESSIBILITY_CONSTRAINTS: PluginCapability.Service.Type
        TYPE_FIELD_NUMBER: _ClassVar[int]
        type: PluginCapability.Service.Type
        def __init__(self, type: _Optional[_Union[PluginCapability.Service.Type, str]] = ...) -> None: ...
    class VolumeExpansion(_message.Message):
        __slots__ = ("type",)
        class Type(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            UNKNOWN: _ClassVar[PluginCapability.VolumeExpansion.Type]
            ONLINE: _ClassVar[PluginCapability.VolumeExpansion.Type]
            OFFLINE: _ClassVar[PluginCapability.VolumeExpansion.Type]
        UNKNOWN: PluginCapability.VolumeExpansion.Type
        ONLINE: PluginCapability.VolumeExpansion.Type
        OFFLINE: PluginCapability.VolumeExpansion.Type
        TYPE_FIELD_NUMBER: _ClassVar[int]
        type: PluginCapability.VolumeExpansion.Type
        def __init__(self, type: _Optional[_Union[PluginCapability.VolumeExpansion.Type, str]] = ...) -> None: ...
    SERVICE_FIELD_NUMBER: _ClassVar[int]
    VOLUME_EXPANSION_FIELD_NUMBER: _ClassVar[int]
    service: PluginCapability.Service
    volume_expansion: PluginCapability.VolumeExpansion
    def __init__(self, service: _Optional[_Union[PluginCapability.Service, _Mapping]] = ..., volume_expansion: _Optional[_Union[PluginCapability.VolumeExpansion, _Mapping]] = ...) -> None: ...

class ProbeRequest(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class ProbeResponse(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class ControllerGetCapabilitiesRequest(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class ControllerGetCapabilitiesResponse(_message.Message):
    __slots__ = ("capabilities",)
    CAPABILITIES_FIELD_NUMBER: _ClassVar[int]
    capabilities: _containers.RepeatedCompositeFieldContainer[ControllerServiceCapability]
    def __init__(self, capabilities: _Optional[_Iterable[_Union[ControllerServiceCapability, _Mapping]]] = ...) -> None: ...

class ControllerServiceCapability(_message.Message):
    __slots__ = ("rpc",)
    class RPC(_message.Message):
        __slots__ = ("type",)
        class Type(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            UNKNOWN: _ClassVar[ControllerServiceCapability.RPC.Type]
            CREATE_DELETE_VOLUME: _ClassVar[ControllerServiceCapability.RPC.Type]
            PUBLISH_UNPUBLISH_VOLUME: _ClassVar[ControllerServiceCapability.RPC.Type]
            LIST_VOLUMES: _ClassVar[ControllerServiceCapability.RPC.Type]
            GET_CAPACITY: _ClassVar[ControllerServiceCapability.RPC.Type]
            CREATE_DELETE_SNAPSHOT: _ClassVar[ControllerServiceCapability.RPC.Type]
            LIST_SNAPSHOTS: _ClassVar[ControllerServiceCapability.RPC.Type]
            CLONE_VOLUME: _ClassVar[ControllerServiceCapability.RPC.Type]
            PUBLISH_READONLY: _ClassVar[ControllerServiceCapability.RPC.Type]
            EXPAND_VOLUME: _ClassVar[ControllerServiceCapability.RPC.Type]
            LIST_VOLUMES_PUBLISHED_NODES: _ClassVar[ControllerServiceCapability.RPC.Type]
            VOLUME_CONDITION: _ClassVar[ControllerServiceCapability.RPC.Type]
            GET_VOLUME: _ClassVar[ControllerServiceCapability.RPC.Type]
            SINGLE_NODE_MULTI_WRITER: _ClassVar[ControllerServiceCapability.RPC.Type]
            MODIFY_VOLUME: _ClassVar[ControllerServiceCapability.RPC.Type]
        UNKNOWN: ControllerServiceCapability.RPC.Type
        CREATE_DELETE_VOLUME: ControllerServiceCapability.RPC.Type
        PUBLISH_UNPUBLISH_VOLUME: ControllerServiceCapability.RPC.Type
        LIST_VOLUMES: ControllerServiceCapability.RPC.Type
        GET_CAPACITY: ControllerServiceCapability.RPC.Type
        CREATE_DELETE_SNAPSHOT: ControllerServiceCapability.RPC.Type
        LIST_SNAPSHOTS: ControllerServiceCapability.RPC.Type
        CLONE_VOLUME: ControllerServiceCapability.RPC.Type
        PUBLISH_READONLY: ControllerServiceCapability.RPC.Type
        EXPAND_VOLUME: ControllerServiceCapability.RPC.Type
        LIST_VOLUMES_PUBLISHED_NODES: ControllerServiceCapability.RPC.Type
        VOLUME_CONDITION: ControllerServiceCapability.RPC.Type
        GET_VOLUME: ControllerServiceCapability.RPC.Type
        SINGLE_NODE_MULTI_WRITER: ControllerServiceCapability.RPC.Type
        MODIFY_VOLUME: ControllerServiceCapability.RPC.Type
        TYPE_FIELD_NUMBER: _ClassVar[int]
        type: ControllerServiceCapability.RPC.Type
        def __init__(self, type: _Optional[_Union[ControllerServiceCapability.RPC.Type, str]] = ...) -> None: ...
    RPC_FIELD_NUMBER: _ClassVar[int]
    rpc: ControllerServiceCapability.RPC
    def __init__(self, rpc: _Optional[_Union[ControllerServiceCapability.RPC, _Mapping]] = ...) -> None: ...

class CreateVolumeRequest(_message.Message):
    __slots__ = ("name", "capacity_range", "volume_capabilities", "parameters", "secrets", "accessibility_requirements", "mutable_parameters")
    class ParametersEntry(_message.Message):
        __slots__ = ("key", "value")
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: str
        def __init__(self, key: _Optional[str] = ..., value: _Optional[str] = ...) -> None: ...
    class SecretsEntry(_message.Message):
        __slots__ = ("key", "value")
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: str
        def __init__(self, key: _Optional[str] = ..., value: _Optional[str] = ...) -> None: ...
    class MutableParametersEntry(_message.Message):
        __slots__ = ("key", "value")
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: str
        def __init__(self, key: _Optional[str] = ..., value: _Optional[str] = ...) -> None: ...
    NAME_FIELD_NUMBER: _ClassVar[int]
    CAPACITY_RANGE_FIELD_NUMBER: _ClassVar[int]
    VOLUME_CAPABILITIES_FIELD_NUMBER: _ClassVar[int]
    PARAMETERS_FIELD_NUMBER: _ClassVar[int]
    SECRETS_FIELD_NUMBER: _ClassVar[int]
    ACCESSIBILITY_REQUIREMENTS_FIELD_NUMBER: _ClassVar[int]
    MUTABLE_PARAMETERS_FIELD_NUMBER: _ClassVar[int]
    name: str
    capacity_range: CapacityRange
    volume_capabilities: _containers.RepeatedCompositeFieldContainer[VolumeCapability]
    parameters: _containers.ScalarMap[str, str]
    secrets: _containers.ScalarMap[str, str]
    accessibility_requirements: AccessibilityRequirements
    mutable_parameters: _containers.ScalarMap[str, str]
    def __init__(self, name: _Optional[str] = ..., capacity_range: _Optional[_Union[CapacityRange, _Mapping]] = ..., volume_capabilities: _Optional[_Iterable[_Union[VolumeCapability, _Mapping]]] = ..., parameters: _Optional[_Mapping[str, str]] = ..., secrets: _Optional[_Mapping[str, str]] = ..., accessibility_requirements: _Optional[_Union[AccessibilityRequirements, _Mapping]] = ..., mutable_parameters: _Optional[_Mapping[str, str]] = ...) -> None: ...

class CreateVolumeResponse(_message.Message):
    __slots__ = ("volume",)
    VOLUME_FIELD_NUMBER: _ClassVar[int]
    volume: Volume
    def __init__(self, volume: _Optional[_Union[Volume, _Mapping]] = ...) -> None: ...

class DeleteVolumeRequest(_message.Message):
    __slots__ = ("volume_id", "secrets")
    class SecretsEntry(_message.Message):
        __slots__ = ("key", "value")
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: str
        def __init__(self, key: _Optional[str] = ..., value: _Optional[str] = ...) -> None: ...
    VOLUME_ID_FIELD_NUMBER: _ClassVar[int]
    SECRETS_FIELD_NUMBER: _ClassVar[int]
    volume_id: str
    secrets: _containers.ScalarMap[str, str]
    def __init__(self, volume_id: _Optional[str] = ..., secrets: _Optional[_Mapping[str, str]] = ...) -> None: ...

class DeleteVolumeResponse(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class ControllerExpandVolumeRequest(_message.Message):
    __slots__ = ("volume_id", "capacity_range", "secrets", "volume_capability")
    class SecretsEntry(_message.Message):
        __slots__ = ("key", "value")
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: str
        def __init__(self, key: _Optional[str] = ..., value: _Optional[str] = ...) -> None: ...
    VOLUME_ID_FIELD_NUMBER: _ClassVar[int]
    CAPACITY_RANGE_FIELD_NUMBER: _ClassVar[int]
    SECRETS_FIELD_NUMBER: _ClassVar[int]
    VOLUME_CAPABILITY_FIELD_NUMBER: _ClassVar[int]
    volume_id: str
    capacity_range: CapacityRange
    secrets: _containers.ScalarMap[str, str]
    volume_capability: VolumeCapability
    def __init__(self, volume_id: _Optional[str] = ..., capacity_range: _Optional[_Union[CapacityRange, _Mapping]] = ..., secrets: _Optional[_Mapping[str, str]] = ..., volume_capability: _Optional[_Union[VolumeCapability, _Mapping]] = ...) -> None: ...

class ControllerExpandVolumeResponse(_message.Message):
    __slots__ = ("capacity_bytes", "node_expansion_required")
    CAPACITY_BYTES_FIELD_NUMBER: _ClassVar[int]
    NODE_EXPANSION_REQUIRED_FIELD_NUMBER: _ClassVar[int]
    capacity_bytes: int
    node_expansion_required: bool
    def __init__(self, capacity_bytes: _Optional[int] = ..., node_expansion_required: bool = ...) -> None: ...

class ValidateVolumeCapabilitiesRequest(_message.Message):
    __slots__ = ("volume_id", "volume_capabilities", "volume_context", "parameters", "secrets")
    class VolumeContextEntry(_message.Message):
        __slots__ = ("key", "value")
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: str
        def __init__(self, key: _Optional[str] = ..., value: _Optional[str] = ...) -> None: ...
    class ParametersEntry(_message.Message):
        __slots__ = ("key", "value")
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: str
        def __init__(self, key: _Optional[str] = ..., value: _Optional[str] = ...) -> None: ...
    class SecretsEntry(_message.Message):
        __slots__ = ("key", "value")
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: str
        def __init__(self, key: _Optional[str] = ..., value: _Optional[str] = ...) -> None: ...
    VOLUME_ID_FIELD_NUMBER: _ClassVar[int]
    VOLUME_CAPABILITIES_FIELD_NUMBER: _ClassVar[int]
    VOLUME_CONTEXT_FIELD_NUMBER: _ClassVar[int]
    PARAMETERS_FIELD_NUMBER: _ClassVar[int]
    SECRETS_FIELD_NUMBER: _ClassVar[int]
    volume_id: str
    volume_capabilities: _containers.RepeatedCompositeFieldContainer[VolumeCapability]
    volume_context: _containers.ScalarMap[str, str]
    parameters: _containers.ScalarMap[str, str]
    secrets: _containers.ScalarMap[str, str]
    def __init__(self, volume_id: _Optional[str] = ..., volume_capabilities: _Optional[_Iterable[_Union[VolumeCapability, _Mapping]]] = ..., volume_context: _Optional[_Mapping[str, str]] = ..., parameters: _Optional[_Mapping[str, str]] = ..., secrets: _Optional[_Mapping[str, str]] = ...) -> None: ...

class ValidateVolumeCapabilitiesResponse(_message.Message):
    __slots__ = ("confirmed", "message")
    class Confirmed(_message.Message):
        __slots__ = ("volume_capabilities", "volume_context", "parameters")
        class VolumeContextEntry(_message.Message):
            __slots__ = ("key", "value")
            KEY_FIELD_NUMBER: _ClassVar[int]
            VALUE_FIELD_NUMBER: _ClassVar[int]
            key: str
            value: str
            def __init__(self, key: _Optional[str] = ..., value: _Optional[str] = ...) -> None: ...
        class ParametersEntry(_message.Message):
            __slots__ = ("key", "value")
            KEY_FIELD_NUMBER: _ClassVar[int]
            VALUE_FIELD_NUMBER: _ClassVar[int]
            key: str
            value: str
            def __init__(self, key: _Optional[str] = ..., value: _Optional[str] = ...) -> None: ...
        VOLUME_CAPABILITIES_FIELD_NUMBER: _ClassVar[int]
        VOLUME_CONTEXT_FIELD_NUMBER: _ClassVar[int]
        PARAMETERS_FIELD_NUMBER: _ClassVar[int]
        volume_capabilities: _containers.RepeatedCompositeFieldContainer[VolumeCapability]
        volume_context: _containers.ScalarMap[str, str]
        parameters: _containers.ScalarMap[str, str]
        def __init__(self, volume_capabilities: _Optional[_Iterable[_Union[VolumeCapability, _Mapping]]] = ..., volume_context: _Optional[_Mapping[str, str]] = ..., parameters: _Optional[_Mapping[str, str]] = ...) -> None: ...
    CONFIRMED_FIELD_NUMBER: _ClassVar[int]
    MESSAGE_FIELD_NUMBER: _ClassVar[int]
    confirmed: ValidateVolumeCapabilitiesResponse.Confirmed
    message: str
    def __init__(self, confirmed: _Optional[_Union[ValidateVolumeCapabilitiesResponse.Confirmed, _Mapping]] = ..., message: _Optional[str] = ...) -> None: ...

class NodeGetCapabilitiesRequest(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class NodeGetCapabilitiesResponse(_message.Message):
    __slots__ = ("capabilities",)
    CAPABILITIES_FIELD_NUMBER: _ClassVar[int]
    capabilities: _containers.RepeatedCompositeFieldContainer[NodeServiceCapability]
    def __init__(self, capabilities: _Optional[_Iterable[_Union[NodeServiceCapability, _Mapping]]] = ...) -> None: ...

class NodeServiceCapability(_message.Message):
    __slots__ = ("rpc",)
    class RPC(_message.Message):
        __slots__ = ("type",)
        class Type(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            UNKNOWN: _ClassVar[NodeServiceCapability.RPC.Type]
            STAGE_UNSTAGE_VOLUME: _ClassVar[NodeServiceCapability.RPC.Type]
            GET_VOLUME_STATS: _ClassVar[NodeServiceCapability.RPC.Type]
            EXPAND_VOLUME: _ClassVar[NodeServiceCapability.RPC.Type]
            VOLUME_CONDITION: _ClassVar[NodeServiceCapability.RPC.Type]
            SINGLE_NODE_MULTI_WRITER: _ClassVar[NodeServiceCapability.RPC.Type]
            VOLUME_MOUNT_GROUP: _ClassVar[NodeServiceCapability.RPC.Type]
        UNKNOWN: NodeServiceCapability.RPC.Type
        STAGE_UNSTAGE_VOLUME: NodeServiceCapability.RPC.Type
        GET_VOLUME_STATS: NodeServiceCapability.RPC.Type
        EXPAND_VOLUME: NodeServiceCapability.RPC.Type
        VOLUME_CONDITION: NodeServiceCapability.RPC.Type
        SINGLE_NODE_MULTI_WRITER: NodeServiceCapability.RPC.Type
        VOLUME_MOUNT_GROUP: NodeServiceCapability.RPC.Type
        TYPE_FIELD_NUMBER: _ClassVar[int]
        type: NodeServiceCapability.RPC.Type
        def __init__(self, type: _Optional[_Union[NodeServiceCapability.RPC.Type, str]] = ...) -> None: ...
    RPC_FIELD_NUMBER: _ClassVar[int]
    rpc: NodeServiceCapability.RPC
    def __init__(self, rpc: _Optional[_Union[NodeServiceCapability.RPC, _Mapping]] = ...) -> None: ...

class NodeGetInfoRequest(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class NodeGetInfoResponse(_message.Message):
    __slots__ = ("node_id", "max_volumes_per_node", "accessible_topology")
    NODE_ID_FIELD_NUMBER: _ClassVar[int]
    MAX_VOLUMES_PER_NODE_FIELD_NUMBER: _ClassVar[int]
    ACCESSIBLE_TOPOLOGY_FIELD_NUMBER: _ClassVar[int]
    node_id: str
    max_volumes_per_node: int
    accessible_topology: Topology
    def __init__(self, node_id: _Optional[str] = ..., max_volumes_per_node: _Optional[int] = ..., accessible_topology: _Optional[_Union[Topology, _Mapping]] = ...) -> None: ...

class NodeStageVolumeRequest(_message.Message):
    __slots__ = ("volume_id", "publish_context", "staging_target_path", "volume_capability", "secrets", "volume_context")
    class PublishContextEntry(_message.Message):
        __slots__ = ("key", "value")
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: str
        def __init__(self, key: _Optional[str] = ..., value: _Optional[str] = ...) -> None: ...
    class SecretsEntry(_message.Message):
        __slots__ = ("key", "value")
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: str
        def __init__(self, key: _Optional[str] = ..., value: _Optional[str] = ...) -> None: ...
    class VolumeContextEntry(_message.Message):
        __slots__ = ("key", "value")
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: str
        def __init__(self, key: _Optional[str] = ..., value: _Optional[str] = ...) -> None: ...
    VOLUME_ID_FIELD_NUMBER: _ClassVar[int]
    PUBLISH_CONTEXT_FIELD_NUMBER: _ClassVar[int]
    STAGING_TARGET_PATH_FIELD_NUMBER: _ClassVar[int]
    VOLUME_CAPABILITY_FIELD_NUMBER: _ClassVar[int]
    SECRETS_FIELD_NUMBER: _ClassVar[int]
    VOLUME_CONTEXT_FIELD_NUMBER: _ClassVar[int]
    volume_id: str
    publish_context: _containers.ScalarMap[str, str]
    staging_target_path: str
    volume_capability: VolumeCapability
    secrets: _containers.ScalarMap[str, str]
    volume_context: _containers.ScalarMap[str, str]
    def __init__(self, volume_id: _Optional[str] = ..., publish_context: _Optional[_Mapping[str, str]] = ..., staging_target_path: _Optional[str] = ..., volume_capability: _Optional[_Union[VolumeCapability, _Mapping]] = ..., secrets: _Optional[_Mapping[str, str]] = ..., volume_context: _Optional[_Mapping[str, str]] = ...) -> None: ...

class NodeStageVolumeResponse(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class NodeUnstageVolumeRequest(_message.Message):
    __slots__ = ("volume_id", "staging_target_path")
    VOLUME_ID_FIELD_NUMBER: _ClassVar[int]
    STAGING_TARGET_PATH_FIELD_NUMBER: _ClassVar[int]
    volume_id: str
    staging_target_path: str
    def __init__(self, volume_id: _Optional[str] = ..., staging_target_path: _Optional[str] = ...) -> None: ...

class NodeUnstageVolumeResponse(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class NodePublishVolumeRequest(_message.Message):
    __slots__ = ("volume_id", "publish_context", "staging_target_path", "target_path", "volume_capability", "readonly", "secrets", "volume_context")
    class PublishContextEntry(_message.Message):
        __slots__ = ("key", "value")
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: str
        def __init__(self, key: _Optional[str] = ..., value: _Optional[str] = ...) -> None: ...
    class SecretsEntry(_message.Message):
        __slots__ = ("key", "value")
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: str
        def __init__(self, key: _Optional[str] = ..., value: _Optional[str] = ...) -> None: ...
    class VolumeContextEntry(_message.Message):
        __slots__ = ("key", "value")
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: str
        def __init__(self, key: _Optional[str] = ..., value: _Optional[str] = ...) -> None: ...
    VOLUME_ID_FIELD_NUMBER: _ClassVar[int]
    PUBLISH_CONTEXT_FIELD_NUMBER: _ClassVar[int]
    STAGING_TARGET_PATH_FIELD_NUMBER: _ClassVar[int]
    TARGET_PATH_FIELD_NUMBER: _ClassVar[int]
    VOLUME_CAPABILITY_FIELD_NUMBER: _ClassVar[int]
    READONLY_FIELD_NUMBER: _ClassVar[int]
    SECRETS_FIELD_NUMBER: _ClassVar[int]
    VOLUME_CONTEXT_FIELD_NUMBER: _ClassVar[int]
    volume_id: str
    publish_context: _containers.ScalarMap[str, str]
    staging_target_path: str
    target_path: str
    volume_capability: VolumeCapability
    readonly: bool
    secrets: _containers.ScalarMap[str, str]
    volume_context: _containers.ScalarMap[str, str]
    def __init__(self, volume_id: _Optional[str] = ..., publish_context: _Optional[_Mapping[str, str]] = ..., staging_target_path: _Optional[str] = ..., target_path: _Optional[str] = ..., volume_capability: _Optional[_Union[VolumeCapability, _Mapping]] = ..., readonly: bool = ..., secrets: _Optional[_Mapping[str, str]] = ..., volume_context: _Optional[_Mapping[str, str]] = ...) -> None: ...

class NodePublishVolumeResponse(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class NodeUnpublishVolumeRequest(_message.Message):
    __slots__ = ("volume_id", "target_path")
    VOLUME_ID_FIELD_NUMBER: _ClassVar[int]
    TARGET_PATH_FIELD_NUMBER: _ClassVar[int]
    volume_id: str
    target_path: str
    def __init__(self, volume_id: _Optional[str] = ..., target_path: _Optional[str] = ...) -> None: ...

class NodeUnpublishVolumeResponse(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class NodeExpandVolumeRequest(_message.Message):
    __slots__ = ("volume_id", "volume_path", "capacity_range", "staging_target_path", "volume_capability")
    VOLUME_ID_FIELD_NUMBER: _ClassVar[int]
    VOLUME_PATH_FIELD_NUMBER: _ClassVar[int]
    CAPACITY_RANGE_FIELD_NUMBER: _ClassVar[int]
    STAGING_TARGET_PATH_FIELD_NUMBER: _ClassVar[int]
    VOLUME_CAPABILITY_FIELD_NUMBER: _ClassVar[int]
    volume_id: str
    volume_path: str
    capacity_range: CapacityRange
    staging_target_path: str
    volume_capability: VolumeCapability
    def __init__(self, volume_id: _Optional[str] = ..., volume_path: _Optional[str] = ..., capacity_range: _Optional[_Union[CapacityRange, _Mapping]] = ..., staging_target_path: _Optional[str] = ..., volume_capability: _Optional[_Union[VolumeCapability, _Mapping]] = ...) -> None: ...

class NodeExpandVolumeResponse(_message.Message):
    __slots__ = ("capacity_bytes",)
    CAPACITY_BYTES_FIELD_NUMBER: _ClassVar[int]
    capacity_bytes: int
    def __init__(self, capacity_bytes: _Optional[int] = ...) -> None: ...

class CapacityRange(_message.Message):
    __slots__ = ("required_bytes", "limit_bytes")
    REQUIRED_BYTES_FIELD_NUMBER: _ClassVar[int]
    LIMIT_BYTES_FIELD_NUMBER: _ClassVar[int]
    required_bytes: int
    limit_bytes: int
    def __init__(self, required_bytes: _Optional[int] = ..., limit_bytes: _Optional[int] = ...) -> None: ...

class VolumeCapability(_message.Message):
    __slots__ = ("mount", "block", "access_mode")
    class Block(_message.Message):
        __slots__ = ()
        def __init__(self) -> None: ...
    class Mount(_message.Message):
        __slots__ = ("fs_type", "mount_flags")
        FS_TYPE_FIELD_NUMBER: _ClassVar[int]
        MOUNT_FLAGS_FIELD_NUMBER: _ClassVar[int]
        fs_type: str
        mount_flags: _containers.RepeatedScalarFieldContainer[str]
        def __init__(self, fs_type: _Optional[str] = ..., mount_flags: _Optional[_Iterable[str]] = ...) -> None: ...
    class AccessMode(_message.Message):
        __slots__ = ("mode",)
        class Mode(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            UNKNOWN: _ClassVar[VolumeCapability.AccessMode.Mode]
            SINGLE_NODE_WRITER: _ClassVar[VolumeCapability.AccessMode.Mode]
            SINGLE_NODE_READER_ONLY: _ClassVar[VolumeCapability.AccessMode.Mode]
            MULTI_NODE_READER_ONLY: _ClassVar[VolumeCapability.AccessMode.Mode]
            MULTI_NODE_SINGLE_WRITER: _ClassVar[VolumeCapability.AccessMode.Mode]
            MULTI_NODE_MULTI_WRITER: _ClassVar[VolumeCapability.AccessMode.Mode]
            SINGLE_NODE_SINGLE_WRITER: _ClassVar[VolumeCapability.AccessMode.Mode]
            SINGLE_NODE_MULTI_WRITER: _ClassVar[VolumeCapability.AccessMode.Mode]
        UNKNOWN: VolumeCapability.AccessMode.Mode
        SINGLE_NODE_WRITER: VolumeCapability.AccessMode.Mode
        SINGLE_NODE_READER_ONLY: VolumeCapability.AccessMode.Mode
        MULTI_NODE_READER_ONLY: VolumeCapability.AccessMode.Mode
        MULTI_NODE_SINGLE_WRITER: VolumeCapability.AccessMode.Mode
        MULTI_NODE_MULTI_WRITER: VolumeCapability.AccessMode.Mode
        SINGLE_NODE_SINGLE_WRITER: VolumeCapability.AccessMode.Mode
        SINGLE_NODE_MULTI_WRITER: VolumeCapability.AccessMode.Mode
        MODE_FIELD_NUMBER: _ClassVar[int]
        mode: VolumeCapability.AccessMode.Mode
        def __init__(self, mode: _Optional[_Union[VolumeCapability.AccessMode.Mode, str]] = ...) -> None: ...
    MOUNT_FIELD_NUMBER: _ClassVar[int]
    BLOCK_FIELD_NUMBER: _ClassVar[int]
    ACCESS_MODE_FIELD_NUMBER: _ClassVar[int]
    mount: VolumeCapability.Mount
    block: VolumeCapability.Block
    access_mode: VolumeCapability.AccessMode
    def __init__(self, mount: _Optional[_Union[VolumeCapability.Mount, _Mapping]] = ..., block: _Optional[_Union[VolumeCapability.Block, _Mapping]] = ..., access_mode: _Optional[_Union[VolumeCapability.AccessMode, _Mapping]] = ...) -> None: ...

class Volume(_message.Message):
    __slots__ = ("volume_id", "capacity_bytes", "volume_context", "accessible_topology")
    class VolumeContextEntry(_message.Message):
        __slots__ = ("key", "value")
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: str
        def __init__(self, key: _Optional[str] = ..., value: _Optional[str] = ...) -> None: ...
    VOLUME_ID_FIELD_NUMBER: _ClassVar[int]
    CAPACITY_BYTES_FIELD_NUMBER: _ClassVar[int]
    VOLUME_CONTEXT_FIELD_NUMBER: _ClassVar[int]
    ACCESSIBLE_TOPOLOGY_FIELD_NUMBER: _ClassVar[int]
    volume_id: str
    capacity_bytes: int
    volume_context: _containers.ScalarMap[str, str]
    accessible_topology: _containers.RepeatedCompositeFieldContainer[Topology]
    def __init__(self, volume_id: _Optional[str] = ..., capacity_bytes: _Optional[int] = ..., volume_context: _Optional[_Mapping[str, str]] = ..., accessible_topology: _Optional[_Iterable[_Union[Topology, _Mapping]]] = ...) -> None: ...

class AccessibilityRequirements(_message.Message):
    __slots__ = ("requisite", "preferred")
    REQUISITE_FIELD_NUMBER: _ClassVar[int]
    PREFERRED_FIELD_NUMBER: _ClassVar[int]
    requisite: _containers.RepeatedCompositeFieldContainer[Topology]
    preferred: _containers.RepeatedCompositeFieldContainer[Topology]
    def __init__(self, requisite: _Optional[_Iterable[_Union[Topology, _Mapping]]] = ..., preferred: _Optional[_Iterable[_Union[Topology, _Mapping]]] = ...) -> None: ...

class Topology(_message.Message):
    __slots__ = ("segments",)
    class SegmentsEntry(_message.Message):
        __slots__ = ("key", "value")
        KEY_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        key: str
        value: str
        def __init__(self, key: _Optional[str] = ..., value: _Optional[str] = ...) -> None: ...
    SEGMENTS_FIELD_NUMBER: _ClassVar[int]
    segments: _containers.ScalarMap[str, str]
    def __init__(self, segments: _Optional[_Mapping[str, str]] = ...) -> None: ...
