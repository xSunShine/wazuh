from fastapi import status, Request
from fastapi.responses import JSONResponse

from comms_api.core.events import create_stateful_events
from comms_api.routers.exceptions import HTTPError
from comms_api.routers.utils import timeout
from wazuh.core.exception import WazuhError
from wazuh.core.indexer.models.events import Events


@timeout(30)
async def post_stateful_events(request: Request, events: Events) -> JSONResponse:
    """Handle posting stateful events.

    Parameters
    ----------
    request : Request
        The incoming HTTP request.
    events : Events
        The events to be posted.

    Raises
    ------
    HTTPError
        If there is an error when indexing the events.

    Returns
    -------
    JSONResponse
        The response from the indexer.
    """
    try:
        response = await create_stateful_events(events, request.app.state.batcher_queue)
        return JSONResponse(response)
    except WazuhError as exc:
        raise HTTPError(message=exc.message, status_code=status.HTTP_400_BAD_REQUEST)
