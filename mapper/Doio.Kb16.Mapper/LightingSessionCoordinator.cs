namespace Doio.Kb16.Mapper;

internal readonly record struct LightingValues(byte Hue, byte Saturation, byte Value);

internal interface ILightingTransport
{
    Task<LightingState> ReadLightingAsync(CancellationToken cancellationToken = default);
    Task PreviewLightingAsync(byte hue, byte saturation, byte value, CancellationToken cancellationToken = default);
    Task<LightingState> CommitLightingAsync(byte hue, byte saturation, byte value, CancellationToken cancellationToken = default);
    Task RestoreLightingAsync(LightingState state, CancellationToken cancellationToken = default);
}

internal sealed class ConfigLightingTransport(ConfigProtocolClient client) : ILightingTransport
{
    public Task<LightingState> ReadLightingAsync(CancellationToken cancellationToken = default) => client.ReadLightingAsync(cancellationToken);
    public Task PreviewLightingAsync(byte hue, byte saturation, byte value, CancellationToken cancellationToken = default) => client.PreviewLightingAsync(hue, saturation, value, cancellationToken);
    public Task<LightingState> CommitLightingAsync(byte hue, byte saturation, byte value, CancellationToken cancellationToken = default) => client.CommitLightingAsync(hue, saturation, value, cancellationToken);
    public Task RestoreLightingAsync(LightingState state, CancellationToken cancellationToken = default) => client.RestoreLightingAsync(state, cancellationToken);
}

internal sealed class LightingSessionCoordinator(ILightingTransport transport)
{
    private readonly object _sync = new();
    private LightingValues? _latestPreview;
    private Task _previewTask = Task.CompletedTask;
    private Task<LightingState?>? _restoreTask;
    private LightingState? _original;
    private bool _committed;

    public LightingState? Original
    {
        get { lock (_sync) return _original; }
    }

    public Task<LightingState> ReadCurrentAsync(CancellationToken cancellationToken = default) => transport.ReadLightingAsync(cancellationToken);

    public async Task<LightingState> BeginAsync(CancellationToken cancellationToken = default)
    {
        var current = await transport.ReadLightingAsync(cancellationToken);
        lock (_sync)
        {
            _latestPreview = null;
            _previewTask = Task.CompletedTask;
            _restoreTask = null;
            _original = current;
            _committed = false;
        }
        return current;
    }

    public void QueuePreview(LightingValues values)
    {
        lock (_sync)
        {
            if (_original is null || _committed) return;
            _latestPreview = values;
        }
    }

    public Task FlushPreviewAsync(CancellationToken cancellationToken = default)
    {
        lock (_sync)
        {
            if (!_previewTask.IsCompleted) return _previewTask;
            if (_latestPreview is null) return Task.CompletedTask;
            _previewTask = FlushPreviewCoreAsync(cancellationToken);
            return _previewTask;
        }
    }

    public void DiscardPendingPreview()
    {
        lock (_sync) _latestPreview = null;
    }

    public async Task<LightingState> CommitAsync(LightingValues values, CancellationToken cancellationToken = default)
    {
        DiscardPendingPreview();
        await IgnorePreviewFailureAsync();
        var saved = await transport.CommitLightingAsync(values.Hue, values.Saturation, values.Value, cancellationToken);
        lock (_sync)
        {
            _original = null;
            _committed = true;
            _restoreTask = null;
        }
        return saved;
    }

    public Task<LightingState?> RestoreAsync(CancellationToken cancellationToken = default)
    {
        lock (_sync)
        {
            if (_committed || _original is null) return Task.FromResult<LightingState?>(null);
            _restoreTask ??= RestoreCoreAsync(_original, cancellationToken);
            return _restoreTask;
        }
    }

    public void Abandon()
    {
        lock (_sync)
        {
            _latestPreview = null;
            _original = null;
            _committed = true;
        }
    }

    private async Task FlushPreviewCoreAsync(CancellationToken cancellationToken)
    {
        try
        {
            while (true)
            {
                LightingValues? next;
                lock (_sync)
                {
                    next = _latestPreview;
                    _latestPreview = null;
                }
                if (next is null) return;
                await transport.PreviewLightingAsync(next.Value.Hue, next.Value.Saturation, next.Value.Value, cancellationToken);
            }
        }
        catch
        {
            DiscardPendingPreview();
            throw;
        }
    }

    private async Task<LightingState?> RestoreCoreAsync(LightingState original, CancellationToken cancellationToken)
    {
        await Task.Yield();
        try
        {
            DiscardPendingPreview();
            await IgnorePreviewFailureAsync();
            await transport.RestoreLightingAsync(original, cancellationToken);
            lock (_sync) _original = null;
            return original;
        }
        catch
        {
            lock (_sync) _restoreTask = null;
            throw;
        }
    }

    private async Task IgnorePreviewFailureAsync()
    {
        Task preview;
        lock (_sync) preview = _previewTask;
        try { await preview; }
        catch (Exception) { }
    }
}
