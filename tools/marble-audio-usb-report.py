"""Audit the explicit post-auth RAM-only USB diagnostic contract.

This is a subcheck; it never substitutes for PCM, PAS or cold-reset reports.
"""
import re


def inspect(text):
    rows = [line for line in text.splitlines() if line.startswith('aud-usb-policy:')]
    errors = []
    expected = dict(version=1, post_auth_RAM_only=1, capture_enabled=0,
                    thermal_unchanged=1, PAS_required=1, audio_acceptance=0)
    values = {}
    if len(rows) != 1:
        errors.append('exactly one RAM-only USB policy record required')
    else:
        pairs = re.findall(r'(\w+)=(\d+)', rows[0])
        values = {k: int(v) for k, v in pairs}
        if len(values) != len(pairs) or set(values) != set(expected) | {'INFO_suppressed'}:
            errors.append('policy fields missing, duplicate or unexpected')
        if any(values.get(k) != v for k, v in expected.items()):
            errors.append('post-auth RAM-only policy changed')
        if not 1 <= values.get('INFO_suppressed', 0) <= 100000:
            errors.append('no bounded post-auth INFO suppression evidence')
    return dict(passed=not errors, errors=errors, record=values,
                audio_acceptance=False,
                scope='RAM policy record only; complete PCM/PAS/reset audited separately')
