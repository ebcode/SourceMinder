"""Convert between SWE-bench instance ids and Docker eval image names.

SWE-bench munges ``__`` to ``_1776_`` for Docker tag safety, so::

    django__django-10554  <->  swebench/sweb.eval.x86_64.django_1776_django-10554:latest

``pre_index.py`` (id -> image) and ``build_pool.py`` (image -> id) each hardcoded
half of this; keeping the prefix and the swap in one place keeps the two
directions inverse.
"""
from __future__ import annotations

IMAGE_PREFIX = "swebench/sweb.eval.x86_64."


def image_of(instance_id: str, tag: str = "latest") -> str:
    """``django__django-10554`` -> ``swebench/sweb.eval.x86_64.django_1776_django-10554:latest``."""
    return f"{IMAGE_PREFIX}{instance_id.replace('__', '_1776_')}:{tag}"


def instance_id_of(image: str) -> str:
    """``...x86_64.django_1776_django-10554:latest`` -> ``django__django-10554``."""
    stem = image.split(IMAGE_PREFIX, 1)[-1].split(":", 1)[0]
    return stem.replace("_1776_", "__")
