"""Dashboard feature APIs for Bertrand's Kubernetes control plane."""

from bertrand.env.kube.dashboard.backend import (
    DASHBOARD_LABEL,
    DASHBOARD_LABEL_VALUE,
    DASHBOARD_LABELS,
    DASHBOARD_NAME,
    HEADLAMP_IMAGE,
    delete_dashboard_backend,
    ensure_dashboard_backend,
)

__all__ = [
    "DASHBOARD_LABEL",
    "DASHBOARD_LABELS",
    "DASHBOARD_LABEL_VALUE",
    "DASHBOARD_NAME",
    "HEADLAMP_IMAGE",
    "delete_dashboard_backend",
    "ensure_dashboard_backend",
]
