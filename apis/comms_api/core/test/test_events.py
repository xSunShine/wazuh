import pytest
from unittest.mock import patch, AsyncMock

from comms_api.core.events import create_stateful_events
from wazuh.core.indexer import Indexer
from wazuh.core.indexer.models.events import Events, SCAEvent

INDEXER = Indexer(host='host', user='wazuh', password='wazuh')


@pytest.mark.asyncio
@patch('wazuh.core.indexer.create_indexer', return_value=AsyncMock())
async def test_create_stateful_events(create_indexer_mock):
    """Test creating stateful events with mocked indexer client."""
    create_indexer_mock.return_value.events.create = AsyncMock()
    batcher_queue = AsyncMock()

    events = Events(events=[SCAEvent()])
    await create_stateful_events(events, batcher_queue)

    create_indexer_mock.assert_called_once()
    create_indexer_mock.return_value.events.create.assert_called_once()
