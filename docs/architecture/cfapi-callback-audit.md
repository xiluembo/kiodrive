<!--
SPDX-FileCopyrightText: 2026 KIODrive authors
SPDX-License-Identifier: CC0-1.0
-->

# Cloud Files callback audit

Reviewed against the Microsoft Cloud Files API documentation on 2026-09-15.

## References

- [CF_CALLBACK_REGISTRATION](https://learn.microsoft.com/windows/win32/api/cfapi/ns-cfapi-cf_callback_registration)
- [CF_CALLBACK_INFO](https://learn.microsoft.com/windows/win32/api/cfapi/ns-cfapi-cf_callback_info)
- [CF_CALLBACK_PARAMETERS](https://learn.microsoft.com/windows/win32/api/cfapi/ns-cfapi-cf_callback_parameters)
- [CF_OPERATION_INFO](https://learn.microsoft.com/windows/win32/api/cfapi/ns-cfapi-cf_operation_info)
- [CF_OPERATION_PARAMETERS](https://learn.microsoft.com/windows/win32/api/cfapi/ns-cfapi-cf_operation_parameters)
- [CfExecute](https://learn.microsoft.com/windows/win32/api/cfapi/nf-cfapi-cfexecute)
- [CfConnectSyncRoot](https://learn.microsoft.com/windows/win32/api/cfapi/nf-cfapi-cfconnectsyncroot)
- [CF_HYDRATION_POLICY](https://learn.microsoft.com/windows/win32/api/cfapi/ns-cfapi-cf_hydration_policy)

## Results

| Callback | Result | Notes |
| --- | --- | --- |
| `FETCH_PLACEHOLDERS` | Conformant for the current full-directory population model | The handler completes the request with `TRANSFER_PLACEHOLDERS`, retains all strings and identities through `CfExecute`, and marks the directory fully populated. It must not use the disable-on-demand flag if pagination is introduced later. |
| `CANCEL_FETCH_PLACEHOLDERS` | Conformant | Cancellation is correlated with the callback's `TransferKey`, rather than its path, so simultaneous requests for one directory cannot overwrite or cancel one another. |
| `FETCH_DATA` | Conformant for bounded range reads | The required offset and length are copied before returning from the callback. A successful transfer covers exactly the required range. Failed and cancelled operations report the complete requested range to `CfExecute`, not a zero-length range. |
| `CANCEL_FETCH_DATA` | Conformant, with deferred I/O interruption | The exact `TransferKey` is cancelled. The underlying synchronous backend read is allowed to finish, after which the request is completed as cancelled; this avoids cancelling unrelated transfers globally. A future asynchronous backend may use the registry operation ID for targeted interruption. |
| `VALIDATE_DATA` | Intentionally not registered | The sync root uses `CF_HYDRATION_POLICY_FULL` without `CF_HYDRATION_POLICY_MODIFIER_VALIDATION_REQUIRED`. The former handler only checked object existence and therefore could incorrectly acknowledge corrupt bytes; it was removed. If validation-required policy is enabled, implement byte or trusted-hash validation before registering this callback. |
| File open/close completion | Conformant | Callback-owned data is copied before queued Qt delivery. A close marked deleted is not reported as a normal content close. |
| `NOTIFY_DEHYDRATE` and completion | Conformant | The blocking notification is acknowledged with `ACK_DEHYDRATE`; application work is queued, and completion is observed separately. The current policy always permits local dehydration. |
| `NOTIFY_RENAME` and completion | Conformant for allow-local-first synchronization | The blocking rename is acknowledged with `ACK_RENAME`; remote synchronization is deferred until the completion callback. Source and target scope flags come from the pre-operation callback, while completion paths are normalized before delivery. The current policy always permits local renames. |
| `NOTIFY_DELETE` and completion | Conformant for allow-local-first synchronization | The blocking delete is acknowledged with `ACK_DELETE`; remote synchronization is deferred until completion. The current policy always permits local deletion. |

## Invariants

1. Treat `CF_CALLBACK_INFO` and `CF_CALLBACK_PARAMETERS` as callback-lifetime data. Copy every path, identity, key, offset, length, and flag needed by queued work.
2. Correlate fetch and cancellation callbacks by `TransferKey`. Paths are not request identifiers; one file can have concurrent range requests.
3. For `TRANSFER_DATA`, use the exact byte range being completed. On failure, the buffer is null but the offset and length still identify the failed required range.
4. Register `VALIDATE_DATA` only together with validation-required hydration policy and a real content-validation implementation.
5. Keep `CF_OPERATION_INFO::StructSize` and `CF_OPERATION_PARAMETERS::ParamSize` initialized for every `CfExecute` operation.

## Remaining Windows verification

The portable test suite exercises request correlation, but a Windows integration test is still required to exercise the actual Cloud Files platform. It should issue overlapping reads against one placeholder, cancel only one read, verify short backend reads fail the full requested range, and perform rename/delete/dehydrate operations through Explorer while tracing `CfExecute` results.
