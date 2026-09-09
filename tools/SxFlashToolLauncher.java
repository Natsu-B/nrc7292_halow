import FirmwareFlashTool.Main.Configuration;
import FirmwareFlashTool.Main.FirmwareFlashTool;
import FirmwareFlashTool.Main.ImagePool;
import FirmwareFlashTool.Panels.MACAddressPanel;
import FirmwareFlashTool.Panels.MainPanel;
import FirmwareFlashTool.Panels.PathPanel;
import _Library.Firmware.Newrafc;
import _Library.GUI._General.Serial.SerialPortComboBox;
import com.google.gson.Gson;
import java.awt.event.ActionListener;
import java.lang.reflect.Field;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.time.Instant;
import java.util.LinkedHashMap;
import java.util.Map;
import javax.swing.AbstractButton;
import javax.swing.JOptionPane;
import javax.swing.JTabbedPane;
import javax.swing.SwingUtilities;

/** Prefills the unchanged vendor GUI; START is always a human button action. */
public final class SxFlashToolLauncher {
    static Field field(Class<?> type, String name) throws Exception {
        Field f = type.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }
    static Object get(Object object, String name) throws Exception {
        return field(object.getClass(), name).get(object);
    }
    static void debugFlags() throws Exception {
        field(Newrafc.class, "DEBUG").setBoolean(null, true);
        field(Newrafc.class, "DEEP_DEBUG").setBoolean(null, false);
    }
    static Map<String, Object> image(String name) throws Exception {
        Map<String, Object> data = new LinkedHashMap<>();
        if (name == null || name.isEmpty()) return data;
        Path path = Path.of(name).toAbsolutePath().normalize();
        data.put("path", path.toString());
        if (Files.isRegularFile(path)) {
            byte[] bytes = Files.readAllBytes(path);
            StringBuilder hex = new StringBuilder();
            for (byte b : MessageDigest.getInstance("SHA-256").digest(bytes))
                hex.append(String.format("%02x", b & 255));
            data.put("bytes", bytes.length);
            data.put("sha256", hex.toString());
        } else data.put("exists", false);
        return data;
    }
    static void log(String event, Configuration c) throws Exception {
        Map<String, Object> out = new LinkedHashMap<>();
        out.put("event", event);
        out.put("time", Instant.now().toString());
        out.put("module", c.getModuleType());
        out.put("port", c.getSerialPort());
        out.put("baudrate", c.getBaudrate());
        out.put("bootloader", image(c.getBootloaderPath()));
        out.put("xip_bootloader", image(c.getXIPBootPath()));
        out.put("xip_firmware", image(c.getXIPFirmwarePath()));
        out.put("xip_boot_enabled", c.isXIPBootEnabled());
        out.put("xip_firmware_enabled", c.isXIPFirmwareEnabled());
        out.put("mac_write_0", c.getWriteMACAddress0());
        out.put("mac_write_1", c.getWriteMACAddress1());
        out.put("auto_run_after_flash", c.getAutoXIPStart());
        // Never serialize Configuration itself: it contains MAC address fields.
        System.out.println(new Gson().toJson(out));
        System.out.flush();
    }
    static boolean selectionAllowed(Configuration c, String[] expected) {
        return expected[0].equals(c.getSerialPort()) && "NRC7292".equals(c.getModuleType()) &&
               expected[1].equals(c.getBootloaderPath()) && expected[2].equals(c.getXIPBootPath()) &&
               expected[3].equals(c.getXIPFirmwarePath()) && c.isXIPBootEnabled() &&
               c.isXIPFirmwareEnabled() && !c.getAutoXIPStart() && c.getBaudrate() == 460800 &&
               !c.getWriteMACAddress0() && !c.getWriteMACAddress1();
    }
    static void check() throws Exception {
        debugFlags();
        for (String name : new String[]{"instructionsTabbedPane", "serialPortComboBox",
                "bootloaderPathPanel", "xipBootPathPanel", "xipFirmwarePathPanel",
                "macAddressPanel0", "macAddressPanel1", "operationPanel"})
            field(MainPanel.class, name);
        field(FirmwareFlashTool.class, "mainPanel");
        Class<?> operation = Class.forName("FirmwareFlashTool.Panels.OperationPanel");
        field(operation, "startButton");
        field(operation, "autoStartCheckBox");
        Configuration c = new Configuration();
        String[] expected = {"expected", "boot", "xip_boot", "firmware"};
        c.setSerialPort("expected");
        c.setModuleType("NRC7292");
        c.setBootloaderPath("boot"); c.setXIPBootPath("xip_boot"); c.setXIPFirmwarePath("firmware");
        c.setXIPBootEnabled(true); c.setXIPFirmwareEnabled(true); c.setBaudrate(460800);
        if (!selectionAllowed(c, expected)) throw new AssertionError();
        c.setWriteMACAddress0(true);
        if (selectionAllowed(c, expected)) throw new AssertionError();
        c.setWriteMACAddress0(false);
        c.setWriteMACAddress1(true);
        if (selectionAllowed(c, expected)) throw new AssertionError();
        c.setWriteMACAddress1(false);
        for (int i = 0; i < expected.length; i++) {
            String[] changed = expected.clone(); changed[i] = "other";
            if (selectionAllowed(c, changed)) throw new AssertionError();
        }
        c.setModuleType("NRC7393");
        if (selectionAllowed(c, expected)) throw new AssertionError();
        System.out.println("LAUNCHER_CHECK PASS: vendor reflection/selection guard; GUI=0 serial_open=0");
    }
    static void launch(String[] args) throws Exception {
        debugFlags();
        ImagePool.load();
        FirmwareFlashTool window = new FirmwareFlashTool();
        MainPanel panel = (MainPanel)get(window, "mainPanel");
        log("sx_gui_original_defaults", panel.generateConfiguration());
        JTabbedPane tabs = (JTabbedPane)get(panel, "instructionsTabbedPane");
        boolean selected = false;
        for (int i = 0; i < tabs.getTabCount(); i++) {
            if (tabs.getTitleAt(i).equals("NRC7292")) {
                tabs.setSelectedIndex(i);
                selected = true;
                break;
            }
        }
        if (!selected) throw new IllegalStateException("Expected NRC7292 tab missing");
        SerialPortComboBox ports = (SerialPortComboBox)get(panel, "serialPortComboBox");
        ports.addItem(args[0]);
        ports.setSelectedItem(args[0]);
        ((PathPanel)get(panel, "bootloaderPathPanel")).setText(args[1]);
        PathPanel xipBoot = (PathPanel)get(panel, "xipBootPathPanel");
        xipBoot.setText(args[2]);
        xipBoot.setSelected(true);
        PathPanel xipFw = (PathPanel)get(panel, "xipFirmwarePathPanel");
        xipFw.setText(args[3]);
        xipFw.setSelected(true);
        ((MACAddressPanel)get(panel, "macAddressPanel0")).uncheckMACWriteCheckBox();
        ((MACAddressPanel)get(panel, "macAddressPanel1")).uncheckMACWriteCheckBox();
        Object operation = get(panel, "operationPanel");
        ((AbstractButton)get(operation, "autoStartCheckBox")).setSelected(false);
        Map<String, Object> expectedBoot = image(args[1]);
        Map<String, Object> expectedXipBoot = image(args[2]);
        Map<String, Object> expectedFirmware = image(args[3]);
        AbstractButton start = (AbstractButton)get(operation, "startButton");
        ActionListener[] vendor = start.getActionListeners();
        if (vendor.length != 1) throw new IllegalStateException("Unexpected vendor START listeners");
        start.removeActionListener(vendor[0]);
        start.addActionListener(event -> {
            try {
                Configuration current = panel.generateConfiguration();
                log("sx_gui_human_start", current);
                if (!selectionAllowed(current, args) || !expectedBoot.equals(image(args[1])) ||
                    !expectedXipBoot.equals(image(args[2])) || !expectedFirmware.equals(image(args[3]))) {
                    System.out.println("SX_GUI_START_BLOCKED: board/port/images/options changed from prepared Stage-1 selection");
                    JOptionPane.showMessageDialog(window,
                        "Restore the prepared NRC7292, SX port, images and options; MAC WRITE must stay off.",
                        "Stage-1 selection check", JOptionPane.ERROR_MESSAGE);
                    return;
                }
                vendor[0].actionPerformed(event);
            } catch (Exception failure) {
                failure.printStackTrace();
                JOptionPane.showMessageDialog(window, "Could not record/check START settings; see launcher log.");
            }
        });
        log("sx_gui_prefilled", panel.generateConfiguration());
        if (args.length == 5 && args[4].equals("--gui-check")) {
            Configuration current = panel.generateConfiguration();
            if (!selectionAllowed(current, args) || !current.isXIPBootEnabled() ||
                !current.isXIPFirmwareEnabled() || !args[1].equals(current.getBootloaderPath()) ||
                !args[2].equals(current.getXIPBootPath()) || !args[3].equals(current.getXIPFirmwarePath()))
                throw new IllegalStateException("GUI prefill check failed");
            window.dispose();
            System.out.println("GUI_CHECK PASS: prefilled vendor GUI; START=0 serial_open=0");
        } else {
            System.out.println("SX_GUI_READY: DEBUG=true DEEP_DEBUG=false; press START manually");
            window.setVisible(true);
        }
    }
    public static void main(String[] args) throws Exception {
        if (args.length == 1 && args[0].equals("--check")) { check(); return; }
        if (args.length != 4 && !(args.length == 5 && args[4].equals("--gui-check")))
            throw new IllegalArgumentException("SX_BY_ID 2ND_BOOT XIP_BOOT XIP_FW [--gui-check]");
        SwingUtilities.invokeAndWait(() -> {
            try { launch(args); }
            catch (Exception error) { throw new RuntimeException(error); }
        });
    }
}
