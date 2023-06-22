# Copyright (C) 2015, Wazuh Inc.
# Created by Wazuh, Inc. <info@wazuh.com>.
# This program is a free software; you can redistribute it and/or modify it under the terms of GPLv2

import logging
from typing import Optional
from aiohttp import web

from api.encoder import dumps, prettify
from api.util import raise_if_exc, parse_api_param
from wazuh.core.cluster.dapi.dapi import DistributedAPI
from wazuh import engine_metrics as metrics

logger = logging.getLogger('wazuh-api')

# TODO Define the max number of limit
HARDCODED_VALUE_TO_SPECIFY = 10000


async def get_metrics(request, scope_name: Optional[str] = None,  instrument_name: Optional[str] = None,
                      select: Optional[str] = None, sort: Optional[str] = None, search: Optional[str] = None,
                      offset: int = 0, limit: int = HARDCODED_VALUE_TO_SPECIFY, pretty: bool = False):
    """Get a single metric or all the collected metrics. Uses the metrics/get and metrics/dump actions
    of the engine.

    Parameters
    ----------
    request : connexion.request
    scope_name: Optional[str]
        Name of the metric scope.  If it is None and the instrument_name is None, returns all
        metrics.
    instrument_name: Optional[str]
        Name of the metric instrument. If it is None and the scope_name is None, returns all
        metrics.
    select : str
        Select which fields to return (separated by comma).
    sort : str
        Sort the collection by a field or fields (separated by comma). Use +/- at the beginning to list in
        ascending or descending order.
    search : str
        Look for elements with the specified string.
    offset : int
        First element to return in the collection.
    limit : int
        Maximum number of elements to return. Default: HARDCODED_VALUE_TO_SPECIFY
    pretty : bool
        Show results in human-readable format.


    Returns
    -------
    TODO
    """

    f_kwargs = {
        'scope_name': scope_name,
        'instrument_name': instrument_name,
        'select': select,
        'sort_by': parse_api_param(sort, 'sort')['fields'] if sort is not None else None,
        'sort_ascending': True if sort is None or parse_api_param(sort, 'sort')['order'] == 'asc' else False,
        'search_text': parse_api_param(search, 'search')['value'] if search is not None else None,
        'complementary_search': parse_api_param(search, 'search')['negation'] if search is not None else None,
        'offset': offset,
        'limit': limit
    }

    dapi = DistributedAPI(f=metrics.get_metrics,
                          f_kwargs=f_kwargs,
                          request_type='local_master',
                          is_async=False,
                          logger=logger,
                          rbac_permissions=request['token_info']['rbac_policies']
                          )

    data = raise_if_exc(await dapi.distribute_function())
    return web.json_response(data=data, status=200, dumps=prettify if pretty else dumps)


async def get_instruments(request, select: Optional[str] = None, sort: Optional[str] = None,
                          search: Optional[str] = None, offset: int = 0, limit: int = HARDCODED_VALUE_TO_SPECIFY,
                          pretty: bool = False):
    """Get all name, status and instruments types. Uses the metrics/list action
    of the engine.

    Parameters
    ----------
    request : connexion.request
    select : Optional[str]
        Select which fields to return (separated by comma).
    sort :Optional[str]
        Sort the collection by a field or fields (separated by comma). Use +/- at the beginning to list in
        ascending or descending order.
    search : Optional[str]
        Look for elements with the specified string.
    offset : int
        First element to return in the collection.
    limit : int
        Maximum number of elements to return. Default: HARDCODED_VALUE_TO_SPECIFY
    pretty : bool
        Show results in human-readable format.


    Returns
    -------
    TODO
    """
    f_kwargs = {
        'select': select,
        'sort_by': parse_api_param(sort, 'sort')['fields'] if sort is not None else None,
        'sort_ascending': True if sort is None or parse_api_param(sort, 'sort')['order'] == 'asc' else False,
        'search_text': parse_api_param(search, 'search')['value'] if search is not None else None,
        'complementary_search': parse_api_param(search, 'search')['negation'] if search is not None else None,
        'offset': offset,
        'limit': limit
    }

    dapi = DistributedAPI(f=metrics.get_instruments,
                          f_kwargs=f_kwargs,
                          request_type='local_master',
                          is_async=False,
                          logger=logger,
                          rbac_permissions=request['token_info']['rbac_policies']
                          )

    data = raise_if_exc(await dapi.distribute_function())
    return web.json_response(data=data, status=200, dumps=prettify if pretty else dumps)


async def enable_instrument(request, scope_name: Optional[str] = None, instrument_name: Optional[str] = None,
                            enable: bool = True, pretty: bool = False):
    """Enable or disable a specified metric. Uses the metrics/enable action
    of the engine.

    Parameters
    ----------
    request : connexion.request
    scope_name: Optional[str]
        Name of the metric scope.  If it is None and the instrument_name is None, returns all
        metrics.
    instrument_name: Optional[str]
        Name of the metric instrument. If it is None and the scope_name is None, returns all
        metrics.
    enable: bool
        Enable of disable the metric. True represent enable, false disable.
    pretty : bool
        Show results in human-readable format.

    Returns
    -------
    TODO
    """
    f_kwargs = {
        'scope_name': scope_name,
        'instrument_name': instrument_name,
        'enable': enable
    }

    dapi = DistributedAPI(f=metrics.enable_instrument,
                          f_kwargs=f_kwargs,
                          request_type='local_master',
                          is_async=False,
                          logger=logger,
                          rbac_permissions=request['token_info']['rbac_policies']
                          )

    data = raise_if_exc(await dapi.distribute_function())
    return web.json_response(data=data, status=200, dumps=prettify if pretty else dumps)
