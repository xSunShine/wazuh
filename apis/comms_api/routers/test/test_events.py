import pytest
from unittest.mock import MagicMock, patch, AsyncMock
from fastapi import status

from comms_api.routers.events import post_stateful_events
from comms_api.routers.exceptions import HTTPError
from wazuh.core.exception import WazuhError


@pytest.mark.asyncio
@patch('comms_api.routers.events.create_stateful_events', return_value={'foo': 'bar'})
async def test_post_stateful_events(create_stateful_events_mock):
    """Verify that the `post_stateful_events` handler works as expected."""
    request = MagicMock()
    request.app.state.batcher_queue = AsyncMock()  # Mock the batcher_queue
    events = [{"example": 1}]

    response = await post_stateful_events(request, events)

    create_stateful_events_mock.assert_called_once_with(events, request.app.state.batcher_queue)
    assert response.status_code == status.HTTP_200_OK
    assert response.body == b'{"foo":"bar"}'


@pytest.mark.asyncio
async def test_post_stateful_events_ko():
    """Verify that the `post_stateful_events` handler catches exceptions successfully."""
    request = MagicMock()
    request.app.state.batcher_queue = AsyncMock()  # Mock the batcher_queue
    events = [{"example": 1}]

    code = status.HTTP_400_BAD_REQUEST
    exception = WazuhError(2200)

    with patch('comms_api.routers.events.create_stateful_events', MagicMock(side_effect=exception)):
        with pytest.raises(HTTPError, match=f'{code}: {exception.message}'):
            await post_stateful_events(request, events)
