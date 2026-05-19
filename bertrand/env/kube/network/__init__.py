"""Networking feature APIs for Bertrand's Kubernetes control plane."""

from bertrand.env.kube.network.bootstrap import ensure_network_backend
from bertrand.env.kube.network.gateway import (
    BERTRAND_GATEWAY,
    BERTRAND_GATEWAY_CLASS,
    BERTRAND_GATEWAY_LISTENER,
    BERTRAND_GATEWAY_PORT,
    ENVOY_GATEWAY_CONTROLLER,
    HTTP_ROUTE_LABEL,
    HTTP_ROUTE_LABEL_VALUE,
    HTTP_ROUTE_LABELS,
    bertrand_gateway_parent_refs,
    ensure_bertrand_gateway,
    gateway_api_crd_missing,
)
from bertrand.env.kube.network.profile import (
    NETWORK_PROFILE_KEY,
    NETWORK_PROFILE_LABEL,
    NETWORK_PROFILE_LABEL_VALUE,
    NETWORK_PROFILE_LABELS,
    NETWORK_PROFILE_NAME,
    NetworkProfile,
)
from bertrand.env.kube.network.workload import (
    delete_workload_http_routes,
    delete_workload_network_policy,
    delete_workload_service,
    ensure_workload_http_routes,
    ensure_workload_network_policy,
    ensure_workload_service,
    workload_http_route_name,
    workload_service_ports,
)

__all__ = [
    "BERTRAND_GATEWAY",
    "BERTRAND_GATEWAY_CLASS",
    "BERTRAND_GATEWAY_LISTENER",
    "BERTRAND_GATEWAY_PORT",
    "ENVOY_GATEWAY_CONTROLLER",
    "HTTP_ROUTE_LABEL",
    "HTTP_ROUTE_LABELS",
    "HTTP_ROUTE_LABEL_VALUE",
    "NETWORK_PROFILE_KEY",
    "NETWORK_PROFILE_LABEL",
    "NETWORK_PROFILE_LABELS",
    "NETWORK_PROFILE_LABEL_VALUE",
    "NETWORK_PROFILE_NAME",
    "NetworkProfile",
    "bertrand_gateway_parent_refs",
    "delete_workload_http_routes",
    "delete_workload_network_policy",
    "delete_workload_service",
    "ensure_bertrand_gateway",
    "ensure_network_backend",
    "ensure_workload_http_routes",
    "ensure_workload_network_policy",
    "ensure_workload_service",
    "gateway_api_crd_missing",
    "workload_http_route_name",
    "workload_service_ports",
]
