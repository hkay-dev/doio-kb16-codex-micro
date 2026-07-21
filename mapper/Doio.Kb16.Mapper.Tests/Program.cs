using System.Buffers.Binary;
using System.IO;
using System.Threading;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Doio.Kb16.Mapper;

static void Check(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

if (args.Contains("--device", StringComparer.OrdinalIgnoreCase))
{
    await using var client = ConfigProtocolClient.Connect();
    var capability = await client.HelloAsync();
    var snapshot = await client.ReadConfigurationAsync();
    Check(capability.ConfigLength == DeviceConfiguration.PayloadSize, "device payload size");
    Check(capability.Generation == snapshot.Generation && capability.Crc == snapshot.Crc32, "hello/read metadata mismatch");
    Console.WriteLine($"Device read passed: {client.DeviceName}; generation={snapshot.Generation}; CRC={snapshot.Crc32:X8}");
    return;
}

var golden = new DeviceConfiguration();
var bytes = golden.ToBinary();
Check(bytes.Length == 340, "payload size");
Check(bytes.AsSpan(0, 16).SequenceEqual(Enumerable.Range(0, 16).Select(value => (byte)value).ToArray()), "native order");
Check(bytes.AsSpan(16).IndexOfAnyExcept((byte)0) == -1, "zero action vector");
Check(Crc32.Compute(bytes) == 0xD82701A6u, "cross-language CRC32 golden vector");
Check((bytes.Length + 54) / 55 == 7, "55-byte Channel 3 fragmentation");
foreach (var kind in Enum.GetValues<ActionKind>())
    Check(ActionCatalog.All.Any(option => option.Action.Kind == kind), $"action catalog missing {kind}");
Check(ActionCatalog.All.Any(option => option.Action.Kind == ActionKind.Firmware && option.Action.Code == 14), "guarded bootloader action");
var noLinkAction = new ActionDescriptor { Kind = ActionKind.Firmware, Code = 15 };
Check(!ActionCatalog.Filter("Media", string.Empty).Any(option => option.Action.Kind == noLinkAction.Kind && option.Action.Code == noLinkAction.Code), "category filter test precondition");
var resetEditor = ActionEditorState.ForControl(noLinkAction);
Check(resetEditor.Category == ActionEditorState.AllCategory && resetEditor.Search.Length == 0, "control selection resets action filters");
Check(resetEditor.Options.Contains(resetEditor.SelectedOption!), "reset action list contains current mapping");
Check(resetEditor.SelectedOption?.Name == "NO LINK prompt", "reset action selection shows current mapping");
Check(ActionCatalog.Filter("Keyboard", string.Empty).All(option => option.Action.Kind is ActionKind.Keyboard or ActionKind.None), "composite keyboard category");
Check(ActionCatalog.All.First(option => option.Name == "F24").CodeText == "01 00 0073", "action code presentation");

var editingDraft = new DeviceConfiguration();
var editingBaseline = editingDraft.Clone();
DraftEditing.SwapNative(editingDraft, 0, NativeControl.AG01);
Check(editingDraft.NativeKeys[0] == NativeControl.AG01 && editingDraft.NativeKeys[1] == NativeControl.AG00, "immediate native swap");
Check(editingBaseline.NativeKeys[0] == NativeControl.AG00, "native swap preserves loaded baseline");
var modifiedAction = DraftEditing.WithModifiers(new ActionDescriptor { Kind = ActionKind.Keyboard, Code = 0x04 }, true, false, true, false);
Check(modifiedAction.Modifiers == 0x05 && modifiedAction.Code == 0x04, "immediate modifier update");
var nonKeyboardAction = DraftEditing.WithModifiers(new ActionDescriptor { Kind = ActionKind.Consumer, Code = 1 }, true, true, true, true);
Check(nonKeyboardAction.Modifiers == 0, "non-keyboard modifiers cleared");
Check(AppIdentity.ApplicationUserModelId == "Doio.Kb16.CodexMapper", "stable AppUserModelID");
Check(AppIdentity.ApplyToCurrentProcess() >= 0, "set current-process AppUserModelID");
Check(AppIdentity.ReadCurrentProcessApplicationUserModelId() == AppIdentity.ApplicationUserModelId, "read current-process AppUserModelID");

var begin = new byte[6];
BinaryPrimitives.WriteUInt16LittleEndian(begin, (ushort)bytes.Length);
BinaryPrimitives.WriteUInt32LittleEndian(begin.AsSpan(2), Crc32.Compute(bytes));
Check(begin.SequenceEqual(new byte[] { 0x54, 0x01, 0xA6, 0x01, 0x27, 0xD8 }), "write-begin golden vector");

var helloPayload = new byte[16] { 0, 1, 4, 16, 0x54, 0x01, 2, 0, 0, 0, 0xA6, 0x01, 0x27, 0xD8, 3, 3 };
var hello = ConfigProtocolClient.ParseHelloPayload(helloPayload);
Check(hello.Generation == 2 && hello.Crc == 0xD82701A6u && hello.ConfigLength == 340, "16-byte hello capability response");
var lighting = ConfigProtocolClient.ParseLightingPayload([1, 3, 170, 140, 120]);
Check(lighting.Enabled && lighting.Mode == 3 && lighting.Hue == 170 && lighting.Saturation == 140 && lighting.Value == 120, "lighting response parsing");
Check(LightingValueConverter.ByteToPercent(120) == 47, "saturation byte to percent");
Check(LightingValueConverter.PercentToByte(47) == 120, "saturation percent to byte");
Check(LightingValueConverter.BrightnessByteToPercent(120) == 60, "brightness byte to percent");
Check(LightingValueConverter.BrightnessPercentToByte(60) == 120, "brightness percent to byte");
Check(LightingValueConverter.BrightnessPercentToByte(100) == 200, "brightness maximum matches QMK limit");
Check(LightingValueConverter.HueByteToDegrees(170) == 239, "hue byte to degrees");
Check(LightingValueConverter.DegreesToHueByte(239) == 170, "hue degrees to byte");
try
{
    ConfigProtocolClient.ParseLightingPayload([1, 0, 0, 0, 0]);
    throw new InvalidOperationException("invalid lighting mode accepted");
}
catch (InvalidDataException) { }

var latestTransport = new FakeLightingTransport();
var latestSession = new LightingSessionCoordinator(latestTransport);
await latestSession.BeginAsync();
latestTransport.BlockNextPreview();
latestSession.QueuePreview(new LightingValues(10, 20, 30));
var latestFlush = latestSession.FlushPreviewAsync();
await latestTransport.PreviewStarted.Task.WaitAsync(TimeSpan.FromSeconds(1));
latestSession.QueuePreview(new LightingValues(40, 50, 60));
latestSession.QueuePreview(new LightingValues(70, 80, 90));
latestTransport.ReleasePreview.TrySetResult();
await latestFlush;
Check(latestTransport.PreviewCalls.Select(item => item.Hue).SequenceEqual(new byte[] { 10, 70 }), "lighting latest-wins preview");
Check(latestTransport.MaxConcurrentPreviews == 1, "lighting single preview in flight");

var retryTransport = new FakeLightingTransport { PreviewFailuresRemaining = 1 };
var retrySession = new LightingSessionCoordinator(retryTransport);
await retrySession.BeginAsync();
retrySession.QueuePreview(new LightingValues(1, 2, 3));
try
{
    await retrySession.FlushPreviewAsync();
    throw new InvalidOperationException("lighting preview failure accepted");
}
catch (IOException) { }
retrySession.QueuePreview(new LightingValues(4, 5, 6));
await retrySession.FlushPreviewAsync();
Check(retryTransport.PreviewCalls.Count == 2 && retryTransport.PreviewCalls[^1].Hue == 4, "lighting preview retry");

var restoreTransport = new FakeLightingTransport();
var restoreSession = new LightingSessionCoordinator(restoreTransport);
var restoreOriginal = await restoreSession.BeginAsync();
var restoreA = restoreSession.RestoreAsync();
var restoreB = restoreSession.RestoreAsync();
var restored = await Task.WhenAll(restoreA, restoreB);
Check(restoreTransport.RestoreCount == 1 && restored.Any(item => item == restoreOriginal), "lighting restore deduplicated");

var commitTransport = new FakeLightingTransport();
var commitSession = new LightingSessionCoordinator(commitTransport);
await commitSession.BeginAsync();
var committed = await commitSession.CommitAsync(new LightingValues(11, 22, 33));
Check(committed.Enabled && committed.Mode == 1 && committed.Hue == 11, "lighting commit result");
Check(await commitSession.RestoreAsync() is null && commitTransport.RestoreCount == 0, "lighting commit suppresses restore");

var failedCommitTransport = new FakeLightingTransport { CommitException = new IOException("Simulated save failure") };
var failedCommitSession = new LightingSessionCoordinator(failedCommitTransport);
await failedCommitSession.BeginAsync();
try
{
    await failedCommitSession.CommitAsync(new LightingValues(12, 23, 34));
    throw new InvalidOperationException("lighting commit failure accepted");
}
catch (IOException) { }
Check(await failedCommitSession.RestoreAsync() is not null && failedCommitTransport.RestoreCount == 1, "failed lighting commit remains restorable");

var shutdownTransport = new FakeLightingTransport();
var shutdownSession = new LightingSessionCoordinator(shutdownTransport);
await shutdownSession.BeginAsync();
shutdownTransport.BlockNextPreview();
shutdownSession.QueuePreview(new LightingValues(21, 31, 41));
var shutdownPreview = shutdownSession.FlushPreviewAsync();
await shutdownTransport.PreviewStarted.Task.WaitAsync(TimeSpan.FromSeconds(1));
var shutdownRestore = shutdownSession.RestoreAsync();
await Task.Delay(30);
Check(shutdownTransport.RestoreCount == 0, "shutdown waits for active lighting preview");
shutdownTransport.ReleasePreview.TrySetResult();
await Task.WhenAll(shutdownPreview, shutdownRestore);
Check(shutdownTransport.RestoreCount == 1, "shutdown restores after active lighting preview");

var unsupportedTransport = new FakeLightingTransport { ReadException = new DeviceProtocolException(7, 2, "Unknown operation") };
try
{
    await new LightingSessionCoordinator(unsupportedTransport).BeginAsync();
    throw new InvalidOperationException("unsupported lighting opcode accepted");
}
catch (DeviceProtocolException exception)
{
    Check(exception.Opcode == 7 && exception.StatusCode == 2, "unsupported lighting capability detection");
}

var roundTrip = DeviceConfiguration.FromBinary(bytes);
Check(roundTrip.ToBinary().SequenceEqual(bytes), "binary round trip");
roundTrip.Layers[0].Keys[0] = new ActionDescriptor { Kind = ActionKind.Keyboard, Modifiers = 0x05, Code = 0x04 };
var actionBytes = roundTrip.ToBinary();
Check(actionBytes[40] == 1 && actionBytes[41] == 5 && actionBytes[42] == 4 && actionBytes[43] == 0, "kind/modifiers/code layout");

var duplicate = new DeviceConfiguration();
duplicate.NativeKeys[0] = duplicate.NativeKeys[1];
try
{
    duplicate.Validate();
    throw new InvalidOperationException("duplicate native control accepted");
}
catch (InvalidDataException) { }

var invalid = new DeviceConfiguration();
invalid.Layers[0].Keys[0] = new ActionDescriptor { Kind = ActionKind.Firmware, Code = 20 };
try
{
    invalid.Validate();
    throw new InvalidOperationException("invalid action accepted");
}
catch (InvalidDataException) { }

var oneActionOld = new DeviceConfiguration();
var oneActionNew = oneActionOld.Clone();
oneActionNew.Layers[0].Keys[0] = new ActionDescriptor { Kind = ActionKind.Keyboard, Modifiers = 0x05, Code = 0x04 };
var oneActionDiff = ConfigurationDiff.Compare(oneActionOld, oneActionNew);
Check(oneActionDiff.ChangedControlCount == 1, "single action semantic count");
Check(oneActionDiff.ByteDifference == 3, "single action byte count");
Check(oneActionDiff.Changes[0].Location == "Number layer · Row 1, column 1", "single action location");
Check(oneActionDiff.Changes[0].OldValue == "No action" && oneActionDiff.Changes[0].NewValue == "Ctrl+Alt+A", "single action description");

var modifierOnly = oneActionNew.Clone();
modifierOnly.Layers[0].Keys[0].Modifiers = 0x01;
var modifierDiff = ConfigurationDiff.Compare(oneActionNew, modifierOnly);
Check(modifierDiff.ChangedControlCount == 1 && modifierDiff.ByteDifference == 1, "modifier-only difference");

var nativeSwap = new DeviceConfiguration();
(nativeSwap.NativeKeys[0], nativeSwap.NativeKeys[1]) = (nativeSwap.NativeKeys[1], nativeSwap.NativeKeys[0]);
var nativeDiff = ConfigurationDiff.Compare(new DeviceConfiguration(), nativeSwap);
Check(nativeDiff.ChangedControlCount == 2 && nativeDiff.ByteDifference == 2, "native swap counts physical positions");

var encoderChange = new DeviceConfiguration();
encoderChange.CodexEncoders[1] = new ActionDescriptor { Kind = ActionKind.Consumer, Code = 1 };
var encoderDiff = ConfigurationDiff.Compare(new DeviceConfiguration(), encoderChange);
Check(encoderDiff.ChangedControlCount == 1 && encoderDiff.Changes[0].Location == "Codex layer · Right encoder · Clockwise", "Codex encoder location");

var manyChanges = new DeviceConfiguration();
for (var index = 0; index < 9; index++) manyChanges.Layers[1].Keys[index] = new ActionDescriptor { Kind = ActionKind.Keyboard, Code = (ushort)(0x04 + index) };
var manyDiff = ConfigurationDiff.Compare(new DeviceConfiguration(), manyChanges);
Check(manyDiff.ChangedControlCount == 9, "more than eight semantic changes");

var expected = new DeviceConfiguration();
var expectedCrc = Crc32.Compute(expected.ToBinary());
var current = new DeviceSnapshot(2, expectedCrc, expected.Clone());
Check(ConfigurationApplyGuard.Matches(2, expectedCrc, expected, current), "matching snapshot allowed");
Check(!ConfigurationApplyGuard.Matches(1, expectedCrc, expected, current), "generation mismatch blocked");
Check(!ConfigurationApplyGuard.Matches(2, expectedCrc ^ 1, expected, current), "CRC mismatch blocked");
var changedContent = expected.Clone();
changedContent.Layers[0].Keys[0] = new ActionDescriptor { Kind = ActionKind.Keyboard, Code = 0x04 };
Check(!ConfigurationApplyGuard.Matches(2, expectedCrc, changedContent, current), "content mismatch blocked");

var stalePresentation = DeviceErrorPresenter.Create(new DeviceStateChangedException(), DeviceOperation.Write);
Check(stalePresentation.Title == "Device Configuration Changed" && !stalePresentation.Disconnect, "stale-state presentation");
var busyPresentation = DeviceErrorPresenter.Create(new DeviceProtocolException(5, 7, "A control is still held"), DeviceOperation.Write);
Check(busyPresentation.Title == "Device Busy" && !busyPresentation.Disconnect, "busy presentation");
var verificationPresentation = DeviceErrorPresenter.Create(new ConfigurationVerificationException("Readback mismatch"), DeviceOperation.Write);
Check(verificationPresentation.Title == "Verification Failed" && !verificationPresentation.Disconnect, "verification stays retryable");
var conflictPresentation = DeviceErrorPresenter.Create(new DeviceConnectionException("In use", true), DeviceOperation.Read);
Check(conflictPresentation.Title == "Device Connection Failed" && conflictPresentation.Disconnect, "HID conflict presentation");

try
{
    ConfigProtocolClient.ParseHelloPayload(new byte[] { 7 });
    throw new InvalidOperationException("protocol error status accepted");
}
catch (DeviceProtocolException exception)
{
    Check(exception.Opcode == 1 && exception.StatusCode == 7, "typed protocol status");
}

Exception? wpfFailure = null;
var renderArgumentIndex = Array.FindIndex(args, argument => argument.Equals("--render-ui", StringComparison.OrdinalIgnoreCase));
var renderDirectory = renderArgumentIndex >= 0 && renderArgumentIndex + 1 < args.Length ? Path.GetFullPath(args[renderArgumentIndex + 1]) : null;
var wpfThread = new Thread(() =>
{
    try
    {
        var app = new App { ShutdownMode = ShutdownMode.OnExplicitShutdown };
        app.InitializeComponent();
        Check(app.Resources.Contains("AccentBrush") && app.Resources.Contains("KeycapButtonStyle") && app.Resources.Contains("ModifierStyle") && app.Resources.Contains("LightingSliderStyle"), "WPF theme resources");

        var smokeWindow = new MainWindow { Width = 1180, Height = 760, ShowInTaskbar = false, Left = -10000, Top = -10000 };
        smokeWindow.LoadVisualPreview(VisualPreviewKind.Main);
        smokeWindow.Show();
        smokeWindow.UpdateLayout();
        Check(smokeWindow.IsConnectedPreviewVisible, "connected preview state");
        Check(smokeWindow.LightingToolTipForTesting.Contains("H 239°", StringComparison.Ordinal), "lighting HSV tooltip");
        Check(smokeWindow.LightingPopupUsesCustomPlacementForTesting, "lighting custom popup placement");
        Check(smokeWindow.LightingTabNavigationForTesting == KeyboardNavigationMode.Cycle, "lighting tab cycle");
        Check(smokeWindow.LightingSurfaceForTesting.Stroke is null && !smokeWindow.HasLegacyLightingFocusForTesting, "lighting indicator has no outline");
        Check(!smokeWindow.LightingSliderHasOuterFocusForTesting, "lighting slider has no outer focus frame");
        Check(smokeWindow.DeviceStageCornerRadiusForTesting.BottomLeft == 10 && smokeWindow.DeviceStageCornerRadiusForTesting.BottomRight == 10, "device stage bottom corners");
        Check(smokeWindow.EditorFooterCornerRadiusForTesting.BottomLeft == 10 && smokeWindow.EditorFooterCornerRadiusForTesting.BottomRight == 10, "editor footer bottom corners");
        smokeWindow.ShowLightingErrorForTesting();
        Check(smokeWindow.LightingErrorVisibleForTesting, "lighting inline error state");
        smokeWindow.SetLightingStateForTesting(new LightingState(false, 1, 170, 140, 0));
        Check(smokeWindow.LightingToolTipForTesting.Contains("Off", StringComparison.Ordinal), "lighting off tooltip");
        smokeWindow.OpenLightingPopupForTesting();
        var keySource = PresentationSource.FromVisual(smokeWindow) ?? throw new InvalidOperationException("missing WPF presentation source");
        var escape = new KeyEventArgs(Keyboard.PrimaryDevice, keySource, Environment.TickCount, Key.Escape) { RoutedEvent = Keyboard.PreviewKeyDownEvent };
        smokeWindow.LightingPanelForTesting.RaiseEvent(escape);
        Check(escape.Handled && !smokeWindow.LightingPopupIsOpenForTesting, "lighting Escape cancellation");
        smokeWindow.SetLightingStateForTesting(new LightingState(true, 1, 170, 140, 120));
        if (renderDirectory is not null)
        {
            Directory.CreateDirectory(renderDirectory);
            RenderWindow(smokeWindow, Path.Combine(renderDirectory, "mapper-main-1180x760.png"));
            RenderWindow(smokeWindow, Path.Combine(renderDirectory, "mapper-main-1180x760-125dpi.png"), 120);
            RenderWindow(smokeWindow, Path.Combine(renderDirectory, "mapper-main-1180x760-150dpi.png"), 144);
            RenderWindow(smokeWindow, Path.Combine(renderDirectory, "mapper-main-1180x760-200dpi.png"), 192);
            smokeWindow.PrepareLightingPanelForTesting();
            RenderElement(smokeWindow.LightingPopupChromeForTesting, Path.Combine(renderDirectory, "mapper-lighting-panel.png"));
        }
        smokeWindow.Close();

        var compactWindow = new MainWindow { Width = 980, Height = 650, ShowInTaskbar = false, Left = -10000, Top = -10000 };
        compactWindow.LoadVisualPreview(VisualPreviewKind.Main);
        compactWindow.Show();
        compactWindow.UpdateLayout();
        Check(compactWindow.ActualWidth >= 980 && compactWindow.ActualHeight >= 650, "compact minimum dimensions");
        if (renderDirectory is not null) RenderWindow(compactWindow, Path.Combine(renderDirectory, "mapper-main-980x650.png"));
        compactWindow.Close();

        var disconnectedWindow = new MainWindow { Width = 1180, Height = 760, ShowInTaskbar = false, Left = -10000, Top = -10000 };
        disconnectedWindow.Show();
        disconnectedWindow.UpdateLayout();
        Check(!disconnectedWindow.IsConnectedPreviewVisible, "disconnected safe state");
        Check(disconnectedWindow.LightingToolTipForTesting.Contains("Device not connected", StringComparison.Ordinal), "disconnected lighting tooltip");
        if (renderDirectory is not null) RenderWindow(disconnectedWindow, Path.Combine(renderDirectory, "mapper-disconnected-1180x760.png"));
        disconnectedWindow.Close();

        var errorWindow = new MainWindow { Width = 1180, Height = 760, ShowInTaskbar = false, Left = -10000, Top = -10000 };
        errorWindow.LoadVisualPreview(VisualPreviewKind.Error);
        errorWindow.Show();
        errorWindow.UpdateLayout();
        if (renderDirectory is not null) RenderWindow(errorWindow, Path.Combine(renderDirectory, "mapper-error-1180x760.png"));

        var oldConfig = new DeviceConfiguration();
        var newConfig = oldConfig.Clone();
        newConfig.Layers[2].Keys[0] = new ActionDescriptor { Kind = ActionKind.Keyboard, Code = 0x73 };
        var confirmation = new ApplyConfirmationWindow(ConfigurationDiff.Compare(oldConfig, newConfig)) { Owner = errorWindow, ShowInTaskbar = false, Left = -10000, Top = -10000 };
        confirmation.Show();
        confirmation.UpdateLayout();
        if (renderDirectory is not null) RenderWindow(confirmation, Path.Combine(renderDirectory, "mapper-confirm-dialog.png"));
        confirmation.Close();
        errorWindow.Close();
        app.Shutdown();
    }
    catch (Exception exception) { wpfFailure = exception; }
});
wpfThread.SetApartmentState(ApartmentState.STA);
wpfThread.Start();
wpfThread.Join();
if (wpfFailure is not null) throw new InvalidOperationException("WPF smoke test failed", wpfFailure);

Console.WriteLine("Mapper serialization tests passed");

static void RenderWindow(Window window, string path, double dpi = 96)
{
    window.Dispatcher.Invoke(() => { }, DispatcherPriority.ApplicationIdle);
    window.UpdateLayout();
    var width = Math.Max(1, (int)Math.Ceiling(window.ActualWidth * dpi / 96));
    var height = Math.Max(1, (int)Math.Ceiling(window.ActualHeight * dpi / 96));
    var bitmap = new RenderTargetBitmap(width, height, dpi, dpi, PixelFormats.Pbgra32);
    bitmap.Render(window);
    var encoder = new PngBitmapEncoder();
    encoder.Frames.Add(BitmapFrame.Create(bitmap));
    using var stream = File.Create(path);
    encoder.Save(stream);
}

static void RenderElement(FrameworkElement element, string path)
{
    element.UpdateLayout();
    var width = Math.Max(1, (int)Math.Ceiling(element.ActualWidth));
    var height = Math.Max(1, (int)Math.Ceiling(element.ActualHeight));
    var bitmap = new RenderTargetBitmap(width, height, 96, 96, PixelFormats.Pbgra32);
    bitmap.Render(element);
    var encoder = new PngBitmapEncoder();
    encoder.Frames.Add(BitmapFrame.Create(bitmap));
    using var stream = File.Create(path);
    encoder.Save(stream);
}

sealed class FakeLightingTransport : ILightingTransport
{
    private bool _blockNextPreview;
    private int _activePreviews;

    public LightingState Current { get; private set; } = new(true, 1, 170, 140, 120);
    public Exception? ReadException { get; init; }
    public Exception? CommitException { get; init; }
    public int PreviewFailuresRemaining { get; set; }
    public List<LightingValues> PreviewCalls { get; } = [];
    public int MaxConcurrentPreviews { get; private set; }
    public int RestoreCount { get; private set; }
    public TaskCompletionSource PreviewStarted { get; private set; } = NewCompletionSource();
    public TaskCompletionSource ReleasePreview { get; private set; } = NewCompletionSource();

    public void BlockNextPreview()
    {
        _blockNextPreview = true;
        PreviewStarted = NewCompletionSource();
        ReleasePreview = NewCompletionSource();
    }

    public Task<LightingState> ReadLightingAsync(CancellationToken cancellationToken = default) =>
        ReadException is null ? Task.FromResult(Current) : Task.FromException<LightingState>(ReadException);

    public async Task PreviewLightingAsync(byte hue, byte saturation, byte value, CancellationToken cancellationToken = default)
    {
        var active = Interlocked.Increment(ref _activePreviews);
        MaxConcurrentPreviews = Math.Max(MaxConcurrentPreviews, active);
        PreviewCalls.Add(new LightingValues(hue, saturation, value));
        try
        {
            if (_blockNextPreview)
            {
                _blockNextPreview = false;
                PreviewStarted.TrySetResult();
                await ReleasePreview.Task.WaitAsync(cancellationToken);
            }
            if (PreviewFailuresRemaining > 0)
            {
                PreviewFailuresRemaining--;
                throw new IOException("Simulated preview failure");
            }
            Current = Current with { Enabled = true, Hue = hue, Saturation = saturation, Value = value };
        }
        finally { Interlocked.Decrement(ref _activePreviews); }
    }

    public Task<LightingState> CommitLightingAsync(byte hue, byte saturation, byte value, CancellationToken cancellationToken = default)
    {
        if (CommitException is not null) return Task.FromException<LightingState>(CommitException);
        Current = new LightingState(true, 1, hue, saturation, value);
        return Task.FromResult(Current);
    }

    public Task RestoreLightingAsync(LightingState state, CancellationToken cancellationToken = default)
    {
        RestoreCount++;
        Current = state;
        return Task.CompletedTask;
    }

    private static TaskCompletionSource NewCompletionSource() => new(TaskCreationOptions.RunContinuationsAsynchronously);
}
