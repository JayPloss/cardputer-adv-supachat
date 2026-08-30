from datetime import timedelta
from django.contrib.auth.models import Permission
from django.db import transaction
from django.utils.timezone import now
from authentik.core.models import Group, Token, User
from authentik.flows.models import Flow, FlowStageBinding
from authentik.rbac.models import Role
from authentik.stages.invitation.models import InvitationStage
from authentik.stages.prompt.models import Prompt, PromptStage
from authentik.stages.redirect.models import RedirectStage
from authentik.stages.user_login.models import UserLoginStage
from authentik.stages.user_write.models import UserWriteStage

with transaction.atomic():
    invitation, _ = InvitationStage.objects.update_or_create(name="supachat-invitation-stage", defaults={"continue_flow_without_invitation": False})
    field_specs = (
        ("supachat-enrollment-username", "username", "Username", "username", True, 0),
        ("supachat-enrollment-password", "password", "Password", "password", True, 1),
        ("supachat-enrollment-password-repeat", "password_repeat", "Password (repeat)", "password", True, 2),
        ("supachat-enrollment-name", "name", "Name", "text", True, 3),
        ("supachat-enrollment-email", "email", "Email (optional)", "email", False, 4),
    )
    fields = []
    for object_name, key, label, field_type, required, order in field_specs:
        prompt, _ = Prompt.objects.update_or_create(name=object_name, defaults={"field_key": key, "label": label, "type": field_type, "required": required, "placeholder": label, "placeholder_expression": False, "order": order})
        fields.append(prompt)
    prompts, _ = PromptStage.objects.get_or_create(name="supachat-enrollment-prompts")
    prompts.fields.set(fields)
    login, _ = UserLoginStage.objects.get_or_create(name="supachat-enrollment-user-login")
    flow, _ = Flow.objects.update_or_create(
        slug="supachat-invitation-enrollment",
        defaults={"name": "SupaChat invitation enrollment", "title": "Join SupaChat", "designation": "enrollment", "authentication": "require_unauthenticated"},
    )
    group = Group.objects.get(name="SupaChat Users")
    writer, _ = UserWriteStage.objects.update_or_create(name="supachat-enrollment-user-write", defaults={"user_creation_mode": "always_create", "create_users_group": group, "user_type": "internal", "user_path_template": "users/supachat"})
    redirect, _ = RedirectStage.objects.update_or_create(name="supachat-enrollment-redirect", defaults={"mode": "static", "target_static": "https://supachat.net/?welcome=1", "keep_context": False})
    FlowStageBinding.objects.filter(target=flow).delete()
    for stage, order in ((invitation, 0), (prompts, 10), (writer, 20), (login, 100), (redirect, 110)):
        FlowStageBinding.objects.create(target=flow, stage=stage, order=order)

    service, _ = User.objects.get_or_create(username="supachat-invite-service", defaults={"name": "SupaChat invitation service", "type": "internal_service_account", "path": "goauthentik.io/service-accounts"})
    service.name = "SupaChat invitation service"
    service.type = "internal_service_account"
    service.is_active = True
    service.save()
    role, _ = Role.objects.get_or_create(name="SupaChat invitation creator")
    permission = Permission.objects.get(content_type__app_label="authentik_stages_invitation", codename="add_invitation")
    role.assign_perms(permission)
    service.roles.set([role])
    token, _ = Token.objects.get_or_create(identifier="supachat_invite_service", defaults={"intent": "api", "user": service, "description": "Create single-use SupaChat enrollment invitations"})
    token.user = service
    token.intent = "api"
    token.expiring = True
    token.expires = now() + timedelta(days=90)
    token.save()

print(f"SUPACHAT_AUTHENTIK_API_TOKEN={token.key}")
print(f"SUPACHAT_AUTHENTIK_INVITE_FLOW_ID={flow.pk}")
print("SUPACHAT_AUTHENTIK_API_URL=https://auth.supachat.net/api/v3")
