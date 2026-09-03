"""Idempotently configure SupaChat in the existing Authentik installation.

Run inside the Authentik server container with ``ak shell < this-file``.
Only objects named here are created or updated; existing applications are untouched.
"""

from dataclasses import replace

from django.db import transaction

from authentik.brands.models import Brand
from authentik.core.models import Application, Group, User
from authentik.flows.models import Flow, FlowStageBinding
from authentik.outposts.models import DockerServiceConnection, Outpost
from authentik.outposts.tasks import outpost_controller
from authentik.policies.models import PolicyBinding
from authentik.providers.proxy.models import ProxyProvider
from authentik.providers.oauth2.models import OAuth2Provider, RedirectURI
from authentik.stages.identification.models import IdentificationStage


SUPACHAT_CSS = r"""
:root {
  --pf-global--primary-color--100: #9df260;
  --pf-global--primary-color--200: #72ce37;
  --pf-global--BackgroundColor--100: #08110d;
  --pf-global--BackgroundColor--200: #111b16;
  --pf-global--Color--100: #fff7db;
  --pf-global--Color--200: #c8d4cb;
  --ak-accent: #9df260;
}
body, .pf-c-background-image, .pf-v5-c-background-image {
  background: radial-gradient(circle at 18% 12%, #173126 0, #08110d 42%, #040806 100%) !important;
}
.pf-c-login__container, .pf-v5-c-login__container {
  color: #fff7db;
}
.pf-c-card, .pf-v5-c-card, .pf-c-login__main, .pf-v5-c-login__main {
  background: #111b16 !important;
  border: 1px solid #2d4638;
  border-radius: 22px;
  box-shadow: 0 18px 60px rgb(0 0 0 / 45%);
}
.pf-c-button.pf-m-primary, .pf-v5-c-button.pf-m-primary {
  background: #9df260 !important;
  color: #07110d !important;
  border-radius: 999px;
  font-weight: 800;
}
.pf-c-form-control:focus, .pf-v5-c-form-control:focus {
  border-color: #9df260 !important;
  box-shadow: 0 0 0 1px #9df260 !important;
}
body::after {
  content: "Powered by Authentik";
  position: fixed;
  right: 18px;
  bottom: 14px;
  z-index: 1000;
  color: #91a198;
  font: 12px/1.2 ui-sans-serif, system-ui, sans-serif;
}
.pf-c-login__footer, .pf-v5-c-login__footer,
ak-brand-links, [class*="brand-links"], [class*="powered-by"] {
  display: none !important;
}
"""


with transaction.atomic():
    template = ProxyProvider.objects.get(pk=3)
    authentication_flow, _ = Flow.objects.update_or_create(
        slug="supachat-authentication",
        defaults={
            "name": "SupaChat authentication",
            "title": "Welcome to SupaChat",
            "designation": "authentication",
            "authentication": "none",
        },
    )
    identification, _ = IdentificationStage.objects.update_or_create(
        name="supachat-authentication-identification",
        defaults={"user_fields": ["username"], "show_matched_user": False},
    )
    FlowStageBinding.objects.filter(target=authentication_flow).delete()
    FlowStageBinding.objects.create(target=authentication_flow, stage=identification, order=10)
    for binding in FlowStageBinding.objects.filter(target=template.authentication_flow).order_by("order"):
        if binding.stage.__class__.__name__ == "IdentificationStage":
            continue
        FlowStageBinding.objects.create(target=authentication_flow, stage=binding.stage, order=binding.order)
    provider, _ = ProxyProvider.objects.get_or_create(
        name="Provider for SupaChat",
        defaults={
            "authentication_flow": authentication_flow,
            "authorization_flow": template.authorization_flow,
            "invalidation_flow": template.invalidation_flow,
            "mode": "forward_single",
            "external_host": "https://supachat.net",
            "internal_host": "",
            "internal_host_ssl_validation": True,
        },
    )
    provider.authentication_flow = authentication_flow
    provider.authorization_flow = template.authorization_flow
    provider.invalidation_flow = template.invalidation_flow
    provider.mode = "forward_single"
    provider.external_host = "https://supachat.net"
    provider.internal_host = ""
    provider.internal_host_ssl_validation = True
    provider.save()

    application, _ = Application.objects.update_or_create(
        slug="supachat",
        defaults={
            "name": "SupaChat",
            "provider": provider,
            "meta_launch_url": "https://supachat.net",
            "meta_description": "Private SupaChat rooms",
            "meta_publisher": "SupaChat",
        },
    )

    group, _ = Group.objects.get_or_create(name="SupaChat Users")
    for username, name in (
        ("papa", "Papa"),
        ("albie", "Albie"),
        ("julien", "Julien"),
        ("josee", "Josée"),
        ("maman", "Josée"),
        ("vero", "Véro"),
        ("theo", "Théo"),
    ):
        user, _ = User.objects.get_or_create(
            username=username,
            defaults={"name": name, "type": "internal"},
        )
        user.name = name
        user.path = "users/supachat"
        user.save(update_fields=["name", "path"])
        user.groups.add(group)
    PolicyBinding.objects.get_or_create(target=application, group=group, defaults={"order": 0})

    mobile_provider, _ = OAuth2Provider.objects.get_or_create(
        name="Provider for SupaChat Mobile",
        defaults={
            "authentication_flow": authentication_flow,
            "authorization_flow": template.authorization_flow,
            "invalidation_flow": template.invalidation_flow,
            "client_type": "public",
            "client_id": "supachat-android",
            "logout_uri": "supachat://logout",
            "signing_key": template.signing_key,
        },
    )
    mobile_provider.authentication_flow = authentication_flow
    mobile_provider.authorization_flow = template.authorization_flow
    mobile_provider.invalidation_flow = template.invalidation_flow
    mobile_provider.client_type = "public"
    mobile_provider.client_id = "supachat-android"
    mobile_provider.logout_uri = "supachat://logout"
    mobile_provider.signing_key = template.signing_key
    mobile_provider.redirect_uris = [
        RedirectURI(matching_mode="strict", url="supachat://auth/callback"),
        RedirectURI(matching_mode="strict", url="net.supachat.app://auth/callback"),
    ]
    mobile_provider.save()
    mobile_provider.property_mappings.set(template.property_mappings.all())

    mobile_application, _ = Application.objects.update_or_create(
        slug="supachat-mobile",
        defaults={
            "name": "SupaChat Mobile",
            "provider": mobile_provider,
            "meta_launch_url": "blank://blank",
            "meta_description": "SupaChat Android authentication",
            "meta_publisher": "SupaChat",
        },
    )
    PolicyBinding.objects.get_or_create(target=mobile_application, group=group, defaults={"order": 0})

    outpost = Outpost.objects.get(name="authentik Embedded Outpost")
    outpost.providers.add(provider)

    default_brand = Brand.objects.get(default=True)
    Brand.objects.update_or_create(
        domain="auth.supachat.net",
        defaults={
            "default": False,
            "branding_title": "SupaChat",
            "branding_logo": "https://supachat.net/supachat-logo.png",
            "branding_favicon": "https://supachat.net/supachat-logo.png",
            "branding_custom_css": SUPACHAT_CSS,
            "branding_default_flow_background": default_brand.branding_default_flow_background,
            "flow_authentication": default_brand.flow_authentication,
            "flow_invalidation": default_brand.flow_invalidation,
            "flow_recovery": default_brand.flow_recovery,
            "flow_unenrollment": default_brand.flow_unenrollment,
            "flow_user_settings": default_brand.flow_user_settings,
            "flow_device_code": default_brand.flow_device_code,
            "default_application": application,
            "web_certificate": default_brand.web_certificate,
            "attributes": default_brand.attributes,
        },
    )

    docker_connection = DockerServiceConnection.objects.get(name="Local Docker connection")
    supachat_outpost, _ = Outpost.objects.get_or_create(
        name="SupaChat Proxy Outpost",
        defaults={
            "type": "proxy",
            "service_connection": docker_connection,
        },
    )
    supachat_outpost.type = "proxy"
    supachat_outpost.service_connection = docker_connection
    supachat_outpost.config = replace(
        supachat_outpost.config,
        authentik_host="https://auth.supachat.net",
        authentik_host_browser="https://auth.supachat.net",
        authentik_host_insecure=False,
        docker_map_ports=True,
        docker_network="supachat-auth",
    )
    supachat_outpost.save()
    supachat_outpost.providers.add(provider)
    transaction.on_commit(lambda: outpost_controller.send(str(supachat_outpost.pk)))

print("SupaChat application, users, domain brand, and dedicated proxy outpost are configured.")
