using System.ComponentModel;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using System.Windows.Input;
using System.Windows.Shapes;
using System.Windows.Threading;
using Microsoft.Win32;

namespace Doio.Kb16.Mapper;

public partial class MainWindow : Window
{
    private static readonly string[] EncoderNames = ["左旋钮", "右旋钮", "中旋钮"];
    private static readonly string[] EncoderActionNames = ["逆时针", "顺时针", "按压"];
    private readonly List<Button> _keyButtons = [];
    private readonly List<Button> _encoderButtons = [];
    private readonly JsonSerializerOptions _jsonOptions = new() { WriteIndented = true, Converters = { new JsonStringEnumConverter() } };
    private readonly DispatcherTimer _lightingPreviewTimer = new() { Interval = TimeSpan.FromMilliseconds(30) };
    private ConfigProtocolClient? _client;
    private DeviceConfiguration? _loaded;
    private DeviceConfiguration? _working;
    private ControlSelection? _selection;
    private bool _updatingEditor;
    private bool _compact;
    private string _selectedCategory = ActionEditorState.AllCategory;
    private uint _generation;
    private uint _crc;
    private LightingState? _lightingOriginal;
    private bool _updatingLighting;
    private bool _lightingPreviewPending;
    private bool _lightingPreviewInFlight;
    private bool _suppressLightingPopupRestore;
    private bool _suppressLightingPreviewErrors;
    private bool _closing;
    private bool _allowClose;
    private LightingSessionCoordinator? _lightingSession;

    public MainWindow()
    {
        InitializeComponent();
        BuildControls();
        _lightingPreviewTimer.Tick += LightingPreviewTimer_Tick;
        SetEditorEnabled(false);
        UpdateDraftBar();
    }

    internal bool IsCompactForTesting => _compact;
    internal bool IsConnectedPreviewVisible => ConnectedWorkspace.Visibility == Visibility.Visible;
    internal FrameworkElement LightingPanelForTesting => LightingPanel;
    internal string LightingToolTipForTesting => LightingButton.ToolTip?.ToString() ?? string.Empty;
    internal bool LightingErrorVisibleForTesting => LightingError.Visibility == Visibility.Visible;
    internal bool LightingPopupIsOpenForTesting => LightingPopup.IsOpen;
    internal bool LightingPopupUsesCustomPlacementForTesting => LightingPopup.Placement == PlacementMode.Custom && LightingPopup.CustomPopupPlacementCallback is not null;
    internal KeyboardNavigationMode LightingTabNavigationForTesting => KeyboardNavigation.GetTabNavigation(LightingPanel);
    internal Ellipse LightingSurfaceForTesting
    {
        get
        {
            LightingButton.ApplyTemplate();
            return (Ellipse)LightingButton.Template.FindName("LightingSurface", LightingButton);
        }
    }
    internal bool HasLegacyLightingFocusForTesting
    {
        get
        {
            LightingButton.ApplyTemplate();
            return LightingButton.Template.FindName("LightingFocus", LightingButton) is not null;
        }
    }

    internal void SetLightingStateForTesting(LightingState state) => SetLightingIndicator(state);
    internal void ShowLightingErrorForTesting() => ShowLightingError("设备正忙", "请松开按键后重试。");
    internal void OpenLightingPopupForTesting() => LightingPopup.IsOpen = true;

    internal void PrepareLightingPanelForTesting()
    {
        SetLightingSliders(new LightingState(true, 1, 170, 140, 120));
        LightingPanel.Measure(new Size(326, double.PositiveInfinity));
        LightingPanel.Arrange(new Rect(LightingPanel.DesiredSize));
        LightingPanel.UpdateLayout();
    }

    internal void LoadVisualPreview(VisualPreviewKind kind)
    {
        if (kind == VisualPreviewKind.Disconnected) return;
        var configuration = CreateVisualPreviewConfiguration();
        LoadSnapshot(new DeviceSnapshot(2, 0x27F33B5B, configuration));
        SetLightingIndicator(new LightingState(true, 1, 170, 140, 120));
        StatusText.Text = "Work Louder Codex Micro";
        ConnectionLed.Fill = (Brush)FindResource("SuccessBrush");
        ReadButton.Content = "重新读取";
        LayerTabs.SelectedIndex = 3;
        _working!.Layers[2].Keys[0] = new ActionDescriptor { Kind = ActionKind.Keyboard, Code = 0x73 };
        SelectControl(new ControlSelection(ControlType.Key, 0, 0));
        UpdateDraftBar();
        if (kind == VisualPreviewKind.Error)
            ShowInlineError("设备配置已变化", "草稿已保留，请重新读取后检查差异。");
    }

    private static DeviceConfiguration CreateVisualPreviewConfiguration()
    {
        var configuration = new DeviceConfiguration();
        var systemKeys = configuration.Layers[2].Keys;
        systemKeys[1] = new ActionDescriptor { Kind = ActionKind.Firmware, Code = 15 };
        systemKeys[2] = new ActionDescriptor { Kind = ActionKind.Consumer, Code = 4 };
        systemKeys[3] = new ActionDescriptor { Kind = ActionKind.Consumer, Code = 3 };
        systemKeys[4] = new ActionDescriptor { Kind = ActionKind.Consumer, Code = 2 };
        systemKeys[5] = new ActionDescriptor { Kind = ActionKind.Consumer, Code = 1 };
        systemKeys[6] = new ActionDescriptor { Kind = ActionKind.Consumer, Code = 6 };
        systemKeys[7] = new ActionDescriptor { Kind = ActionKind.Consumer, Code = 5 };
        systemKeys[8] = new ActionDescriptor { Kind = ActionKind.Keyboard, Modifiers = 0x08, Code = 0x07 };
        systemKeys[9] = new ActionDescriptor { Kind = ActionKind.Keyboard, Modifiers = 0x08, Code = 0x0F };
        systemKeys[10] = new ActionDescriptor { Kind = ActionKind.Firmware, Code = 6 };
        systemKeys[11] = new ActionDescriptor { Kind = ActionKind.Firmware, Code = 12 };
        systemKeys[12] = new ActionDescriptor { Kind = ActionKind.Firmware, Code = 2 };
        systemKeys[13] = new ActionDescriptor { Kind = ActionKind.Firmware, Code = 3 };
        systemKeys[14] = new ActionDescriptor { Kind = ActionKind.Firmware, Code = 4 };
        systemKeys[15] = new ActionDescriptor { Kind = ActionKind.Firmware, Code = 15 };
        var encoders = configuration.Layers[2].Encoders;
        encoders[0] = new ActionDescriptor { Kind = ActionKind.Consumer, Code = 2 };
        encoders[1] = new ActionDescriptor { Kind = ActionKind.Consumer, Code = 1 };
        encoders[2] = new ActionDescriptor { Kind = ActionKind.Consumer, Code = 3 };
        encoders[3] = new ActionDescriptor { Kind = ActionKind.Consumer, Code = 6 };
        encoders[4] = new ActionDescriptor { Kind = ActionKind.Consumer, Code = 5 };
        encoders[5] = new ActionDescriptor { Kind = ActionKind.Consumer, Code = 4 };
        encoders[6] = new ActionDescriptor { Kind = ActionKind.Firmware, Code = 11 };
        encoders[7] = new ActionDescriptor { Kind = ActionKind.Firmware, Code = 12 };
        encoders[8] = new ActionDescriptor { Kind = ActionKind.Firmware, Code = 2 };
        configuration.Validate();
        return configuration;
    }

    protected override void OnSourceInitialized(EventArgs e)
    {
        base.OnSourceInitialized(e);
        _ = AppIdentity.ApplyToWindow(this);
        AppIdentity.ApplyWarmTitleBar(this);
    }

    private void BuildControls()
    {
        for (var index = 0; index < 16; index++)
        {
            var captured = index;
            var button = new Button
            {
                Style = (Style)FindResource("KeycapButtonStyle"),
                Margin = new Thickness(4),
                Tag = new ControlSelection(ControlType.Key, index, 0),
            };
            button.Click += (_, _) => SelectControl(new ControlSelection(ControlType.Key, captured, 0));
            _keyButtons.Add(button);
            KeyboardGrid.Children.Add(button);
        }

        for (var encoder = 0; encoder < 3; encoder++)
        {
            var panel = new Grid();
            panel.RowDefinitions.Add(new RowDefinition { Height = new GridLength(23) });
            panel.RowDefinitions.Add(new RowDefinition());
            panel.ColumnDefinitions.Add(new ColumnDefinition());
            panel.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(68) });
            panel.ColumnDefinitions.Add(new ColumnDefinition());
            panel.Children.Add(new TextBlock
            {
                Text = EncoderNames[encoder],
                FontWeight = FontWeights.SemiBold,
                FontSize = 11,
                HorizontalAlignment = HorizontalAlignment.Center,
                VerticalAlignment = VerticalAlignment.Center,
            });
            Grid.SetColumnSpan(panel.Children[^1], 3);

            int[] actionIndices = [0, 2, 1];
            for (var position = 0; position < 3; position++)
            {
                var selection = new ControlSelection(ControlType.Encoder, encoder, actionIndices[position]);
                var button = new Button
                {
                    Style = (Style)FindResource(position == 1 ? "KnobButtonStyle" : "EncoderActionButtonStyle"),
                    Tag = selection,
                    Margin = new Thickness(2),
                };
                button.Click += (_, _) => SelectControl((ControlSelection)button.Tag);
                _encoderButtons.Add(button);
                Grid.SetRow(button, 1);
                Grid.SetColumn(button, position);
                panel.Children.Add(button);
            }
            EncoderGrid.Children.Add(new Border
            {
                Margin = new Thickness(4),
                Padding = new Thickness(4, 2, 4, 4),
                Background = new SolidColorBrush(Color.FromArgb(105, 248, 247, 242)),
                CornerRadius = new CornerRadius(12),
                Child = panel,
            });
        }
        RefreshControls();
    }

    private async void ReadDevice_Click(object sender, RoutedEventArgs e)
    {
        var readSucceeded = false;
        await RunDeviceActionAsync(DeviceOperation.Read, async () =>
        {
            await EnsureConnectedAsync();
            var snapshot = await _client!.ReadConfigurationAsync();
            LoadSnapshot(snapshot);
            StatusText.Text = _client.DeviceName;
            ConnectionLed.Fill = (Brush)FindResource("SuccessBrush");
            ReadButton.Content = "重新读取";
            readSucceeded = true;
        });
        if (readSucceeded) await RefreshLightingIndicatorAsync();
    }

    private async Task RefreshLightingIndicatorAsync()
    {
        LightingButton.IsEnabled = false;
        try
        {
            var lighting = await _lightingSession!.ReadCurrentAsync();
            SetLightingIndicator(lighting);
            LightingButton.IsEnabled = true;
        }
        catch (DeviceProtocolException exception) when (exception.Opcode == 7 && exception.StatusCode == 2)
        {
            ResetLightingIndicator("普通层灯光 · 当前固件不支持");
            LightingButton.IsEnabled = false;
            SetLightingToolTip("当前键盘固件不支持灯控；需要配套灯控固件");
        }
        catch (Exception exception)
        {
            await HandleLightingExceptionAsync(exception, LightingFailureContext.Open);
        }
    }

    private async void Apply_Click(object sender, RoutedEventArgs e)
    {
        if (_working is null || _loaded is null) { MessageBox.Show("请先读取设备配置。", "尚未连接"); return; }
        var difference = ConfigurationDiff.Compare(_loaded, _working);
        if (!difference.HasChanges) return;
        var confirmation = new ApplyConfirmationWindow(difference) { Owner = this };
        if (confirmation.ShowDialog() != true) return;
        await RunDeviceActionAsync(DeviceOperation.Write, async () =>
        {
            await EnsureConnectedAsync();
            var current = await _client!.ReadConfigurationAsync();
            if (!ConfigurationApplyGuard.Matches(_generation, _crc, _loaded, current)) throw new DeviceStateChangedException();
            var snapshot = await _client!.WriteConfigurationAsync(_working);
            LoadSnapshot(snapshot);
            StatusText.Text = "配置已写入并通过回读校验";
        });
    }

    private void Revert_Click(object sender, RoutedEventArgs e)
    {
        if (_loaded is null) return;
        _working = _loaded.Clone();
        _selection = null;
        HideInlineError();
        RefreshControls();
        SetEditorEnabled(false);
        UpdateDraftBar();
        StatusText.Text = _client?.DeviceName ?? "已撤销尚未应用的修改";
    }

    private async void Reset_Click(object sender, RoutedEventArgs e)
    {
        if (MessageBox.Show("将恢复与 v1.2 完全一致的默认键位。确定继续？", "恢复默认", MessageBoxButton.OKCancel, MessageBoxImage.Warning) != MessageBoxResult.OK) return;
        await RunDeviceActionAsync(DeviceOperation.Reset, async () =>
        {
            await EnsureConnectedAsync();
            LoadSnapshot(await _client!.ResetDefaultsAsync());
            StatusText.Text = "设备已恢复 v1.2 默认配置";
        });
    }

    private void Export_Click(object sender, RoutedEventArgs e)
    {
        if (_working is null) { MessageBox.Show("没有可导出的配置。"); return; }
        var dialog = new SaveFileDialog { Filter = "KB16 配置 (*.json)|*.json", FileName = "doio-kb16-config-v1.json" };
        if (dialog.ShowDialog(this) == true) File.WriteAllText(dialog.FileName, JsonSerializer.Serialize(_working, _jsonOptions));
    }

    private void Import_Click(object sender, RoutedEventArgs e)
    {
        if (_loaded is null) { MessageBox.Show("请先读取设备配置，再把 JSON 作为当前设备的本地草稿导入。", "请先读取设备"); return; }
        var dialog = new OpenFileDialog { Filter = "KB16 配置 (*.json)|*.json" };
        if (dialog.ShowDialog(this) != true) return;
        try
        {
            var imported = JsonSerializer.Deserialize<DeviceConfiguration>(File.ReadAllText(dialog.FileName), _jsonOptions) ?? throw new InvalidDataException("JSON 内容为空。");
            imported.Validate();
            _working = imported;
            _selection = null;
            HideInlineError();
            RefreshControls();
            SetEditorEnabled(false);
            UpdateDraftBar();
            StatusText.Text = "已导入本地草稿；尚未写入设备";
        }
        catch (Exception exception) { MessageBox.Show(exception.Message, "导入失败", MessageBoxButton.OK, MessageBoxImage.Error); }
    }

    private void More_Click(object sender, RoutedEventArgs e)
    {
        if (MoreButton.ContextMenu is null) return;
        MoreButton.ContextMenu.PlacementTarget = MoreButton;
        MoreButton.ContextMenu.IsOpen = true;
    }

    private async void LightingButton_Click(object sender, RoutedEventArgs e)
    {
        if (LightingPopup.IsOpen)
        {
            LightingPopup.IsOpen = false;
            return;
        }

        IsEnabled = false;
        try
        {
            await EnsureConnectedAsync();
            _lightingOriginal = await _lightingSession!.BeginAsync();
            SetLightingSliders(_lightingOriginal);
            HideLightingError();
            SaveLightingButton.IsEnabled = false;
            LightingPopup.IsOpen = true;
            _ = Dispatcher.BeginInvoke(DispatcherPriority.Input, () => BrightnessSlider.Focus());
        }
        catch (Exception exception) { await HandleLightingExceptionAsync(exception, LightingFailureContext.Open); }
        finally { IsEnabled = true; }
    }

    private void SetLightingSliders(LightingState state)
    {
        _updatingLighting = true;
        try
        {
            BrightnessSlider.Value = LightingValueConverter.ByteToPercent(state.Value);
            HueSlider.Value = LightingValueConverter.HueByteToDegrees(state.Hue);
            SaturationSlider.Value = LightingValueConverter.ByteToPercent(state.Saturation);
            UpdateLightingVisuals();
        }
        finally { _updatingLighting = false; }
    }

    private void LightingSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (BrightnessValueText is null || HueValueText is null || SaturationValueText is null) return;
        if (!_updatingLighting && LightingPopup.IsOpen) HideLightingError();
        UpdateLightingVisuals();
        if (_lightingOriginal is not null && SaveLightingButton is not null)
        {
            var current = CurrentLightingValues();
            SaveLightingButton.IsEnabled = current.Hue != _lightingOriginal.Hue || current.Saturation != _lightingOriginal.Saturation || current.Value != _lightingOriginal.Value || !_lightingOriginal.Enabled || _lightingOriginal.Mode != 1;
        }
        if (_updatingLighting || !LightingPopup.IsOpen || _lightingSession is null) return;
        _lightingPreviewPending = true;
        if (!_lightingPreviewTimer.IsEnabled) _lightingPreviewTimer.Start();
    }

    private async void LightingPreviewTimer_Tick(object? sender, EventArgs e)
    {
        if (!_lightingPreviewPending)
        {
            _lightingPreviewTimer.Stop();
            return;
        }
        _lightingPreviewPending = false;
        if (_lightingSession is null) return;
        var current = CurrentLightingValues();
        _lightingSession.QueuePreview(new LightingValues(current.Hue, current.Saturation, current.Value));
        if (_lightingPreviewInFlight) return;

        _lightingPreviewInFlight = true;
        try
        {
            await _lightingSession.FlushPreviewAsync();
        }
        catch (Exception exception)
        {
            _lightingPreviewTimer.Stop();
            _lightingPreviewPending = false;
            if (!_suppressLightingPreviewErrors && !_closing) await HandleLightingExceptionAsync(exception, LightingFailureContext.Panel);
        }
        finally { _lightingPreviewInFlight = false; }
    }

    private async void SaveLighting_Click(object sender, RoutedEventArgs e)
    {
        if (_client is null || _lightingSession is null) return;
        _lightingPreviewTimer.Stop();
        _lightingPreviewPending = false;
        _lightingSession.DiscardPendingPreview();
        SaveLightingButton.IsEnabled = false;
        var current = CurrentLightingValues();
        _suppressLightingPreviewErrors = true;
        try
        {
            var saved = await _lightingSession.CommitAsync(new LightingValues(current.Hue, current.Saturation, current.Value));
            _lightingOriginal = null;
            SetLightingIndicator(saved);
            HideLightingError();
            StatusText.Text = "普通层灯光已保存并通过回读校验";
            LightingPopup.IsOpen = false;
        }
        catch (Exception exception)
        {
            SaveLightingButton.IsEnabled = true;
            await HandleLightingExceptionAsync(exception, LightingFailureContext.Panel);
        }
        finally { _suppressLightingPreviewErrors = false; }
    }

    private void CancelLighting_Click(object sender, RoutedEventArgs e) => LightingPopup.IsOpen = false;

    private async void LightingPopup_Closed(object? sender, EventArgs e)
    {
        if (!_suppressLightingPopupRestore) await RestoreLightingPreviewAsync(true);
    }

    private async Task RestoreLightingPreviewAsync(bool showErrors, CancellationToken cancellationToken = default)
    {
        _lightingPreviewTimer.Stop();
        _lightingPreviewPending = false;
        if (_lightingSession is null) return;
        _lightingSession.DiscardPendingPreview();
        try
        {
            var restored = await _lightingSession.RestoreAsync(cancellationToken);
            if (restored is not null)
            {
                SetLightingIndicator(restored);
                StatusText.Text = _client?.DeviceName ?? "灯光预览已恢复";
            }
            HideLightingError();
        }
        catch (Exception exception)
        {
            if (showErrors) await HandleLightingExceptionAsync(exception, LightingFailureContext.Restore);
        }
        finally { _lightingOriginal = null; }
    }

    private (byte Hue, byte Saturation, byte Value) CurrentLightingValues() =>
        (LightingValueConverter.DegreesToHueByte(HueSlider.Value), LightingValueConverter.PercentToByte(SaturationSlider.Value), LightingValueConverter.PercentToByte(BrightnessSlider.Value));

    private void UpdateLightingVisuals()
    {
        var brightness = Math.Clamp(BrightnessSlider.Value / 100d, 0, 1);
        var saturation = Math.Clamp(SaturationSlider.Value / 100d, 0, 1);
        var hue = HueSlider.Value;
        BrightnessValueText.Text = $"{Math.Round(BrightnessSlider.Value):0}%";
        HueValueText.Text = $"{Math.Round(hue):0}°";
        SaturationValueText.Text = $"{Math.Round(SaturationSlider.Value):0}%";

        var color = HsvToColor(hue, saturation, brightness);
        LightingColorPreview.Background = new SolidColorBrush(color);
        LightingIndicator.Fill = new SolidColorBrush(color);
        if (LightingPopup.IsOpen || _lightingSession?.Original is not null)
            SetLightingToolTip(BuildLightingToolTip(hue, SaturationSlider.Value, BrightnessSlider.Value));
        BrightnessSlider.Background = new LinearGradientBrush(Color.FromRgb(36, 36, 32), HsvToColor(hue, saturation, 1), 0);
        SaturationSlider.Background = new LinearGradientBrush(HsvToColor(hue, 0, Math.Max(brightness, 0.55)), HsvToColor(hue, 1, Math.Max(brightness, 0.55)), 0);
    }

    private void SetLightingIndicator(LightingState state)
    {
        var color = state.Enabled
            ? HsvToColor(LightingValueConverter.HueByteToDegrees(state.Hue), state.Saturation / 255d, state.Value / 255d)
            : Color.FromRgb(48, 48, 44);
        LightingIndicator.Fill = new SolidColorBrush(color);
        SetLightingToolTip(!state.Enabled || state.Value == 0
            ? "普通层灯光 · 已关闭"
            : BuildLightingToolTip(LightingValueConverter.HueByteToDegrees(state.Hue), LightingValueConverter.ByteToPercent(state.Saturation), LightingValueConverter.ByteToPercent(state.Value)));
    }

    private void ResetLightingIndicator(string tooltip = "普通层灯光 · 设备未连接")
    {
        LightingIndicator.Fill = new SolidColorBrush(Color.FromRgb(142, 141, 134));
        SetLightingToolTip(tooltip);
    }

    private static string BuildLightingToolTip(double hue, double saturation, double value) =>
        $"普通层灯光 · H {Math.Round(hue):0}° · S {Math.Round(saturation):0}% · V {Math.Round(value):0}%";

    private void SetLightingToolTip(string tooltip)
    {
        LightingButton.ToolTip = tooltip;
        AutomationProperties.SetHelpText(LightingButton, tooltip);
    }

    private void ShowLightingError(string title, string message)
    {
        LightingErrorText.Text = $"{title}：{message}";
        LightingError.Visibility = Visibility.Visible;
    }

    private void HideLightingError() => LightingError.Visibility = Visibility.Collapsed;

    private async Task HandleLightingExceptionAsync(Exception exception, LightingFailureContext context)
    {
        var presentation = DeviceErrorPresenter.Create(exception, context == LightingFailureContext.Open ? DeviceOperation.Read : DeviceOperation.Write);
        StatusText.Text = presentation.Title;
        if (!presentation.Disconnect && context == LightingFailureContext.Panel)
        {
            ShowLightingError(presentation.Title, presentation.Message);
            return;
        }
        if (!presentation.Disconnect && context == LightingFailureContext.Open)
        {
            ShowInlineError(presentation.Title, presentation.Message);
            return;
        }

        _suppressLightingPopupRestore = true;
        _lightingSession?.Abandon();
        LightingPopup.IsOpen = false;
        _suppressLightingPopupRestore = false;
        await DisconnectClientAsync();
        ShowInlineError(presentation.Title, context == LightingFailureContext.Restore
            ? "未能恢复打开面板前的灯光状态，请重新读取设备。所有预览均未写入 EEPROM。"
            : "灯光连接已中断，请重新读取设备后重试。");
    }

    private CustomPopupPlacement[] LightingPopup_Placement(Size popupSize, Size targetSize, Point offset) =>
    [
        new CustomPopupPlacement(new Point(targetSize.Width - popupSize.Width, targetSize.Height + 2), PopupPrimaryAxis.Horizontal),
        new CustomPopupPlacement(new Point(targetSize.Width - popupSize.Width, -popupSize.Height - 2), PopupPrimaryAxis.Horizontal),
    ];

    private void LightingPanel_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key != Key.Escape) return;
        e.Handled = true;
        LightingPopup.IsOpen = false;
    }

    private static Color HsvToColor(double hue, double saturation, double value)
    {
        var chroma = value * saturation;
        var sector = hue / 60d;
        var x = chroma * (1 - Math.Abs(sector % 2 - 1));
        var (red, green, blue) = sector switch
        {
            < 1 => (chroma, x, 0d),
            < 2 => (x, chroma, 0d),
            < 3 => (0d, chroma, x),
            < 4 => (0d, x, chroma),
            < 5 => (x, 0d, chroma),
            _ => (chroma, 0d, x),
        };
        var match = value - chroma;
        return Color.FromRgb((byte)Math.Round((red + match) * 255), (byte)Math.Round((green + match) * 255), (byte)Math.Round((blue + match) * 255));
    }

    private void LayerTabs_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (!IsLoaded) return;
        _selection = null;
        LayerNote.Text = $"{LayerName(LayerTabs.SelectedIndex)} 层  16 个按键  3 个旋钮";
        RefreshControls();
        SetEditorEnabled(false);
    }

    private void SelectControl(ControlSelection selection)
    {
        if (_working is null) return;
        _selection = selection;
        HideInlineError();
        SetEditorEnabled(true);
        var layer = LayerTabs.SelectedIndex;
        SelectedControlText.Text = SelectionLocation(selection, layer);
        RefreshControls();
        UpdateEditorSummary();

        // Let WPF render the selected surface before repopulating the action list.
        // This keeps the orange selection response immediate even on software-rendered systems.
        _ = Dispatcher.BeginInvoke(DispatcherPriority.Background, () => PopulateEditor(selection, layer));
    }

    private void PopulateEditor(ControlSelection selection, int layer)
    {
        if (_working is null || _selection != selection || LayerTabs.SelectedIndex != layer) return;
        _updatingEditor = true;
        try
        {
            if (selection.Type == ControlType.Key && layer == 0)
            {
                FilterPanel.IsEnabled = false;
                FilterPanel.Opacity = 0.42;
                ModifierPanel.Visibility = Visibility.Collapsed;
                ActionBox.ItemsSource = Enum.GetValues<NativeControl>()
                    .Select(value => new ActionOption("Codex 原生", NativeName(value), new ActionDescriptor { Kind = ActionKind.None, Code = (ushort)value }))
                    .ToArray();
                ActionBox.SelectedIndex = (int)_working.NativeKeys[selection.Index];
                EditorHint.Text = "选择另一原生控件后会立即交换位置，16 个原生事件始终各保留一次。";
            }
            else
            {
                FilterPanel.IsEnabled = true;
                FilterPanel.Opacity = 1;
                ModifierPanel.Visibility = Visibility.Visible;
                var action = GetSelectedAction();
                ResetActionEditor(action);
                SetModifierChecks(action);
                EditorHint.Text = selection.Type == ControlType.Encoder && selection.ActionIndex != 2
                    ? "旋转动作按一次点击执行；选择后立即进入本地草稿。"
                    : "组合键修饰符仅对键盘动作生效；设备写入仍需点击底部按钮。";
            }
        }
        finally { _updatingEditor = false; }
        UpdateEditorSummary();
    }

    private void ActionBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_updatingEditor || _working is null || _selection is null || ActionBox.SelectedItem is not ActionOption option) return;
        var layer = LayerTabs.SelectedIndex;
        if (_selection.Type == ControlType.Key && layer == 0)
        {
            DraftEditing.SwapNative(_working, _selection.Index, (NativeControl)option.Action.Code);
        }
        else
        {
            var action = DraftEditing.WithModifiers(option.Action, CtrlBox.IsChecked == true, ShiftBox.IsChecked == true, AltBox.IsChecked == true, WinBox.IsChecked == true);
            SetSelectedAction(action);
            SetModifierEnabled(action.Kind == ActionKind.Keyboard);
        }
        RefreshControls();
        UpdateEditorSummary();
        UpdateDraftBar();
    }

    private void Modifier_Changed(object sender, RoutedEventArgs e)
    {
        if (_updatingEditor || _working is null || _selection is null) return;
        if (_selection.Type == ControlType.Key && LayerTabs.SelectedIndex == 0) return;
        var current = GetSelectedAction();
        if (current.Kind != ActionKind.Keyboard) return;
        SetSelectedAction(DraftEditing.WithModifiers(current, CtrlBox.IsChecked == true, ShiftBox.IsChecked == true, AltBox.IsChecked == true, WinBox.IsChecked == true));
        RefreshControls();
        UpdateEditorSummary();
        UpdateDraftBar();
    }

    private void Category_Click(object sender, RoutedEventArgs e)
    {
        if (_updatingEditor || sender is not RadioButton { Tag: string category }) return;
        _selectedCategory = category;
        ApplyActionFilter();
    }

    private void Filter_Changed(object sender, TextChangedEventArgs e)
    {
        SearchWatermark.Visibility = string.IsNullOrEmpty(SearchBox.Text) ? Visibility.Visible : Visibility.Collapsed;
        if (!_updatingEditor && _selection is not null && !(_selection.Type == ControlType.Key && LayerTabs.SelectedIndex == 0)) ApplyActionFilter();
    }

    private void ApplyActionFilter()
    {
        var current = _selection is null ? null : GetSelectedAction();
        _updatingEditor = true;
        try
        {
            var options = ActionCatalog.Filter(_selectedCategory, SearchBox.Text);
            ActionBox.ItemsSource = options;
            ActionBox.SelectedItem = current is null ? null : options.FirstOrDefault(option => option.Action.Kind == current.Kind && option.Action.Code == current.Code);
            QueueSelectedActionIntoView();
        }
        finally { _updatingEditor = false; }
    }

    private void ResetActionEditor(ActionDescriptor action)
    {
        var state = ActionEditorState.ForControl(action);
        _selectedCategory = state.Category;
        CategoryAll.IsChecked = true;
        SearchBox.Text = state.Search;
        ActionBox.ItemsSource = state.Options;
        ActionBox.SelectedItem = state.SelectedOption;
        QueueSelectedActionIntoView();
    }

    private void QueueSelectedActionIntoView()
    {
        if (ActionBox.SelectedItem is null) return;
        _ = ActionBox.Dispatcher.BeginInvoke(DispatcherPriority.Loaded, () => ActionBox.ScrollIntoView(ActionBox.SelectedItem));
    }

    private void SetModifierChecks(ActionDescriptor action)
    {
        CtrlBox.IsChecked = (action.Modifiers & 0x01) != 0;
        ShiftBox.IsChecked = (action.Modifiers & 0x02) != 0;
        AltBox.IsChecked = (action.Modifiers & 0x04) != 0;
        WinBox.IsChecked = (action.Modifiers & 0x08) != 0;
        SetModifierEnabled(action.Kind == ActionKind.Keyboard);
    }

    private void SetModifierEnabled(bool enabled)
    {
        CtrlBox.IsEnabled = ShiftBox.IsEnabled = AltBox.IsEnabled = WinBox.IsEnabled = enabled;
    }

    private ActionDescriptor GetSelectedAction()
    {
        var selection = _selection!;
        var layer = LayerTabs.SelectedIndex;
        if (selection.Type == ControlType.Key) return _working!.Layers[layer - 1].Keys[selection.Index];
        if (layer == 0) return _working!.CodexEncoders[(selection.Index - 1) * 3 + selection.ActionIndex];
        return _working!.Layers[layer - 1].Encoders[selection.Index * 3 + selection.ActionIndex];
    }

    private ActionDescriptor GetLoadedAction(ControlSelection selection, int layer)
    {
        if (selection.Type == ControlType.Key) return _loaded!.Layers[layer - 1].Keys[selection.Index];
        if (layer == 0) return _loaded!.CodexEncoders[(selection.Index - 1) * 3 + selection.ActionIndex];
        return _loaded!.Layers[layer - 1].Encoders[selection.Index * 3 + selection.ActionIndex];
    }

    private void SetSelectedAction(ActionDescriptor action)
    {
        var selection = _selection!;
        var layer = LayerTabs.SelectedIndex;
        if (selection.Type == ControlType.Key) _working!.Layers[layer - 1].Keys[selection.Index] = action;
        else if (layer == 0) _working!.CodexEncoders[(selection.Index - 1) * 3 + selection.ActionIndex] = action;
        else _working!.Layers[layer - 1].Encoders[selection.Index * 3 + selection.ActionIndex] = action;
    }

    private void RefreshControls()
    {
        if (_keyButtons.Count == 0) return;
        var layer = LayerTabs.SelectedIndex;
        for (var index = 0; index < 16; index++)
        {
            var selection = new ControlSelection(ControlType.Key, index, 0);
            var name = _working is null ? $"K{index + 1}" : layer == 0 ? NativeName(_working.NativeKeys[index]) : ActionCatalog.Describe(_working.Layers[layer - 1].Keys[index]);
            var changed = _loaded is not null && _working is not null && (layer == 0
                ? _loaded.NativeKeys[index] != _working.NativeKeys[index]
                : !DraftEditing.IsSame(_loaded.Layers[layer - 1].Keys[index], _working.Layers[layer - 1].Keys[index]));
            _keyButtons[index].Content = KeycapContent(name, index, changed);
            ApplySelectionSurface(_keyButtons[index], _selection == selection, false);
        }

        foreach (var button in _encoderButtons)
        {
            var selection = (ControlSelection)button.Tag;
            var locked = layer == 0 && selection.Index == 0;
            button.IsEnabled = _working is not null && !locked;
            var name = locked ? NativeEncoderName(selection.ActionIndex) : _working is null ? "—" : ActionCatalog.Describe(layer == 0
                ? _working.CodexEncoders[(selection.Index - 1) * 3 + selection.ActionIndex]
                : _working.Layers[layer - 1].Encoders[selection.Index * 3 + selection.ActionIndex]);
            button.Content = selection.ActionIndex == 2 ? null : EncoderButtonContent(name);
            button.ToolTip = locked ? "Codex 层左旋钮保持原生映射" : name;
            ApplySelectionSurface(button, _selection == selection, selection.ActionIndex == 2);
        }
        LayerNote.Text = $"{LayerName(layer)} 层  16 个按键  3 个旋钮";
    }

    private object KeycapContent(string name, int index, bool changed)
    {
        var grid = new Grid();
        var stack = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
        stack.Children.Add(new TextBlock { Text = name, FontWeight = FontWeights.SemiBold, FontSize = 12 });
        stack.Children.Add(new TextBlock { Text = $"第 {index / 4 + 1} 排 / 第 {index % 4 + 1} 列", Foreground = (Brush)FindResource("MutedBrush"), FontSize = 9, Margin = new Thickness(0, 3, 0, 0) });
        grid.Children.Add(stack);
        if (changed)
        {
            grid.Children.Add(new Ellipse { Width = 6, Height = 6, Fill = Brushes.FloralWhite, HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Top });
        }
        return grid;
    }

    private object EncoderButtonContent(string action)
    {
        return new TextBlock
        {
            Text = action,
            FontSize = 9,
            FontWeight = FontWeights.SemiBold,
            TextAlignment = TextAlignment.Center,
            TextWrapping = TextWrapping.Wrap,
            HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Center,
        };
    }

    private void ApplySelectionSurface(Button button, bool selected, bool knob)
    {
        if (knob)
        {
            button.Background = selected ? (Brush)FindResource("AccentSoftBrush") : new SolidColorBrush(Color.FromRgb(236, 235, 230));
            button.Foreground = (Brush)FindResource("InkBrush");
            return;
        }

        if (button.Tag is ControlSelection { Type: ControlType.Encoder })
        {
            button.Background = Brushes.Transparent;
            button.Foreground = selected ? (Brush)FindResource("AccentBrush") : (Brush)FindResource("InkBrush");
            button.FontWeight = selected ? FontWeights.Bold : FontWeights.SemiBold;
            return;
        }

        button.Background = selected ? (Brush)FindResource("AccentBrush") : (Brush)FindResource("ControlBrush");
        button.Foreground = selected ? (Brush)FindResource("AccentInkBrush") : (Brush)FindResource("InkBrush");
    }

    private void UpdateEditorSummary()
    {
        if (_selection is null || _working is null) return;
        var layer = LayerTabs.SelectedIndex;
        var description = _selection.Type == ControlType.Key && layer == 0
            ? NativeName(_working.NativeKeys[_selection.Index])
            : ActionCatalog.Describe(GetSelectedAction());
        CurrentActionText.Text = description;
        var changed = _loaded is not null && (_selection.Type == ControlType.Key && layer == 0
            ? _loaded.NativeKeys[_selection.Index] != _working.NativeKeys[_selection.Index]
            : !DraftEditing.IsSame(GetLoadedAction(_selection, layer), GetSelectedAction()));
        CurrentDraftMark.Text = changed ? "未应用" : string.Empty;
    }

    private void UpdateDraftBar()
    {
        if (_loaded is null || _working is null)
        {
            DraftTitle.Text = "等待读取设备";
            DraftSubtitle.Text = "连接后会在这里显示未应用修改和设备快照";
            SnapshotText.Text = string.Empty;
            RevertButton.IsEnabled = ApplyButton.IsEnabled = false;
            return;
        }
        var difference = ConfigurationDiff.Compare(_loaded, _working);
        SnapshotText.Text = $"DEVICE SNAPSHOT\nGEN {_generation:D2} / {_crc:X8}";
        DraftTitle.Text = difference.HasChanges ? $"{difference.ChangedControlCount} 处未应用修改" : "当前没有未应用修改";
        DraftSubtitle.Text = difference.HasChanges
            ? $"{difference.Changes[0].Location}：{difference.Changes[0].OldValue} → {difference.Changes[0].NewValue}"
            : "所有编辑都只保存在本地草稿，应用前不会写入设备";
        RevertButton.IsEnabled = ApplyButton.IsEnabled = difference.HasChanges;
    }

    private async Task EnsureConnectedAsync()
    {
        if (_client is not null)
        {
            _lightingSession ??= new LightingSessionCoordinator(new ConfigLightingTransport(_client));
            return;
        }
        var client = ConfigProtocolClient.Connect();
        try
        {
            var hello = await client.HelloAsync();
            if (hello.ConfigLength != DeviceConfiguration.PayloadSize) throw new InvalidDataException("固件配置长度与编辑器不匹配。");
            _client = client;
            _lightingSession = new LightingSessionCoordinator(new ConfigLightingTransport(client));
        }
        catch
        {
            await client.DisposeAsync();
            throw;
        }
    }

    private void LoadSnapshot(DeviceSnapshot snapshot)
    {
        _generation = snapshot.Generation;
        _crc = snapshot.Crc32;
        _loaded = snapshot.Configuration;
        _working = snapshot.Configuration.Clone();
        _selection = null;
        ConfigMetaText.Text = $"GEN {_generation:D2}   CRC {_crc:X8}   CHANNEL 3";
        ConnectedWorkspace.Visibility = Visibility.Visible;
        EmptyState.Visibility = Visibility.Collapsed;
        LightingButton.IsEnabled = true;
        HideInlineError();
        RefreshControls();
        SetEditorEnabled(false);
        UpdateDraftBar();
    }

    private async Task RunDeviceActionAsync(DeviceOperation operation, Func<Task> action)
    {
        IsEnabled = false;
        try
        {
            await action();
            HideInlineError();
        }
        catch (Exception exception)
        {
            await PresentDeviceExceptionAsync(exception, operation);
        }
        finally { IsEnabled = true; }
    }

    private async Task PresentDeviceExceptionAsync(Exception exception, DeviceOperation operation)
    {
        var presentation = DeviceErrorPresenter.Create(exception, operation);
        StatusText.Text = presentation.Title;
        if (exception is DeviceStateChangedException)
            ShowInlineError(presentation.Title, "草稿已保留，请重新读取后检查差异。");
        else
            MessageBox.Show(presentation.Message, presentation.Title, MessageBoxButton.OK, MessageBoxImage.Error);
        if (presentation.Disconnect && _client is not null)
            await DisconnectClientAsync();
    }

    private async Task DisconnectClientAsync()
    {
        _lightingSession?.Abandon();
        _lightingSession = null;
        _lightingOriginal = null;
        try
        {
            if (_client is not null) await _client.DisposeAsync();
        }
        finally
        {
            _client = null;
            LightingButton.IsEnabled = false;
            ResetLightingIndicator();
            ConnectionLed.Fill = new SolidColorBrush(Color.FromRgb(170, 169, 161));
            ReadButton.Content = "连接 / 读取";
        }
    }

    private void ShowInlineError(string title, string message)
    {
        InlineErrorTitle.Text = title;
        InlineErrorMessage.Text = message;
        InlineError.Visibility = Visibility.Visible;
    }

    private void HideInlineError() => InlineError.Visibility = Visibility.Collapsed;

    private void SetEditorEnabled(bool enabled)
    {
        ActionBox.IsEnabled = enabled;
        FilterPanel.IsEnabled = enabled;
        ModifierPanel.IsEnabled = enabled;
        if (enabled) return;
        SelectedControlText.Text = "请选择按键或旋钮动作";
        CurrentActionText.Text = "—";
        CurrentDraftMark.Text = string.Empty;
        ActionBox.ItemsSource = null;
        SearchBox.Text = string.Empty;
        CategoryAll.IsChecked = true;
        ModifierPanel.Visibility = Visibility.Visible;
        EditorHint.Text = "选择一个物理控件后即可编辑本地草稿。";
    }

    private void Window_SizeChanged(object sender, SizeChangedEventArgs e)
    {
        var compact = ActualWidth <= 1040 || ActualHeight <= 690;
        if (_compact == compact || !IsLoaded) return;
        _compact = compact;
        TopRow.Height = new GridLength(compact ? 58 : 64);
        DraftRow.Height = new GridLength(compact ? 62 : 68);
        BrandColumn.Width = new GridLength(compact ? 230 : 278);
        BrandSubtitle.Visibility = compact ? Visibility.Collapsed : Visibility.Visible;
        ConnectedWorkspace.Margin = new Thickness(compact ? 10 : 14);
        EmptyState.Margin = new Thickness(compact ? 10 : 14);
        MainGapColumn.Width = new GridLength(compact ? 10 : 14);
        WorkspaceHeaderRow.Height = new GridLength(compact ? 48 : 54);
        DeviceStage.Padding = new Thickness(compact ? 10 : 18, 10, compact ? 10 : 18, compact ? 8 : 12);
        HardwareShell.Padding = new Thickness(compact ? 10 : 14);
        EncoderRow.Height = new GridLength(compact ? 102 : 116);
        EditorHeader.Margin = new Thickness(compact ? 12 : 16, compact ? 10 : 14, compact ? 12 : 16, 8);
        var horizontalDraftMargin = compact ? 14 : 18;
        DraftBarGrid.Margin = new Thickness(horizontalDraftMargin, 0, horizontalDraftMargin, 0);
    }

    protected override async void OnClosing(CancelEventArgs e)
    {
        base.OnClosing(e);
        if (e.Cancel || _allowClose) return;
        e.Cancel = true;
        if (_closing) return;
        _closing = true;
        IsEnabled = false;
        _lightingPreviewTimer.Stop();
        _lightingPreviewPending = false;
        _lightingSession?.DiscardPendingPreview();
        _suppressLightingPopupRestore = true;
        LightingPopup.IsOpen = false;
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(7));
        try
        {
            await RestoreLightingPreviewAsync(false, timeout.Token);
            if (_client is not null) await _client.DisposeAsync();
        }
        catch (OperationCanceledException) { }
        catch (Exception) { }
        finally
        {
            _client = null;
            _lightingSession = null;
            _allowClose = true;
            _ = Dispatcher.BeginInvoke(DispatcherPriority.Normal, Close);
        }
    }

    protected override void OnClosed(EventArgs e)
    {
        _lightingPreviewTimer.Stop();
        base.OnClosed(e);
    }

    private static string SelectionLocation(ControlSelection selection, int layer) => selection.Type == ControlType.Key
        ? $"{LayerName(layer)}层 / 第 {selection.Index / 4 + 1} 排 / 第 {selection.Index % 4 + 1} 列"
        : $"{LayerName(layer)}层 / {EncoderNames[selection.Index]} / {EncoderActionNames[selection.ActionIndex]}";

    private static string LayerName(int layer) => new[] { "CODEX", "数字", "导航", "系统" }[layer];
    private static string NativeEncoderName(int action) => action switch { 0 => "原生逆时针", 1 => "原生顺时针", _ => "原生按压" };
    private static string NativeName(NativeControl value) => value switch
    {
        NativeControl.JoystickUp => "摇杆上",
        NativeControl.JoystickRight => "摇杆右",
        NativeControl.JoystickDown => "摇杆下",
        NativeControl.JoystickLeft => "摇杆左",
        NativeControl.MicACT10 => "Mic ACT10",
        _ => value.ToString(),
    };

    private enum ControlType { Key, Encoder }
    private enum LightingFailureContext { Open, Panel, Restore }
    private sealed record ControlSelection(ControlType Type, int Index, int ActionIndex);
}

internal enum VisualPreviewKind { Disconnected, Main, Error }
