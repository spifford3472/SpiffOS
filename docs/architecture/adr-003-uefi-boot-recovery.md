# ADR-003 — UEFI boot, recovery, and A/B updates

Status: ACCEPTED (policy); implementation details OPEN
Date: 2026-10-20

## Decision
- Boot natively via a small custom x86-64 UEFI loader directly to the BASIC READY environment, without a normal boot menu or deliberate countdown.
- Maintain two independent bootable OS slots (A/B), separate from user data. Updates are written and verified in the inactive slot without overwriting the known-good slot.
- Keep a small fallback menu in the loader, plus a separate recovery image independent of the normal BASIC interpreter.
- Manual recovery is available via F8 during UEFI startup, and via a one-time recovery request from the Ctrl+Alt+Delete emergency supervisor.
- Automatic recovery/rollback applies to failed pending updates and unusable boot images, using bounded attempts and explicit boot-success health checks (kernel, supervisor, BASIC readiness).
- Power loss after a successfully confirmed boot must not by itself trigger rollback. Unexpected power loss during a pending update is ambiguous and must be handled conservatively.
- Avoid normal-boot delays for recovery key polling; account for firmware keyboard variability.
- Develop initially with Secure Boot disabled; design for future user-managed signing support.

## Safety and limitations
A/B protects bootable OS images, not automatically user files or incompatible data migrations. Boot metadata updates must tolerate interrupted writes; boot-success must be recorded atomically/durably. Recovery and data-backup policy are distinct.

## Open engineering details
Exact boot retry count; slot and recovery image formats; integrity/signature scheme; boot metadata persistence; rollback state machine; keyboard behavior on target hardware; recovery tool scope; measured boot targets. Test in QEMU/OVMF before physical laptop deployment.
