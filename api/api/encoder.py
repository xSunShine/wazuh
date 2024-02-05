# Copyright (C) 2015, Wazuh Inc.
# Created by Wazuh, Inc. <info@wazuh.com>.
# This program is a free software; you can redistribute it and/or modify it under the terms of GPLv2

import orjson

import six
from connexion.jsonifier import JSONEncoder

from api.models.base_model_ import Model
from wazuh.core.results import AbstractWazuhResult


def default(o: object) -> dict:
    """Override the default method of the JSONEncoder class.

    Parameters
    ----------
    o : object
        Object to be encoded as JSON.

    Returns
    -------
    dict
        Dictionary representing the object.
    """
    if isinstance(o, Model):
        result = {}
        for attr, _ in six.iteritems(o.swagger_types):
            value = getattr(o, attr)
            if value is None:
                continue
            attr = o.attribute_map[attr]
            result[attr] = value
        return result
    elif isinstance(o, AbstractWazuhResult):
        return o.render()
    return JSONEncoder.default(JSONEncoder, o)

def dumps(obj: object) -> str:
    """Get a JSON encoded str from an object.

    Parameters
    ----------
    obj: object
        Object to be encoded in a JSON string.

    Raises
    ------
    TypeError

    Returns
    -------
    str
    """
    return orjson.dumps(obj, default=default).decode('utf-8')


def prettify(obj: object) -> str:
    """Get a prettified JSON encoded str from an object.

    Parameters
    ----------
    obj: object
        Object to be encoded in a JSON string.

    Raises
    ------
    TypeError

    Returns
    -------
    str
    """
    return orjson.dumps(obj, default=default, option=orjson.OPT_INDENT_2).decode('utf-8')
