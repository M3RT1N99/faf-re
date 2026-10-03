package com.faforever.faf;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.Locale;

public final class LauncherActivity extends Activity {
    private static final int PICK_FAF = 1;
    private static final int PICK_SCFA = 2;
    private static final String PREFS = "faf_launcher";

    private SharedPreferences preferences;
    private LinearLayout content;
    private Spinner renderer;
    private EditText resolution;
    private EditText frameLimit;
    private CheckBox vsync;
    private CheckBox logsEnabled;
    private TextView fafStatus;
    private TextView scfaStatus;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        preferences = getSharedPreferences(PREFS, MODE_PRIVATE);
        appendLog("Launcher opened");

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(20), dp(18), dp(20), dp(28));
        content.setBackgroundColor(0xff101820);
        scroll.addView(content);

        TextView title = text("Forged Alliance Forever", 25, 0xffffffff);
        title.setGravity(Gravity.CENTER_VERTICAL);
        content.addView(title, matchWrap());
        content.addView(text("Android launcher  " + getVersionName(), 13, 0xffa9bac8),
                margins(matchWrap(), 0, dp(4), 0, dp(18)));

        addHeading("Game files");
        content.addView(text("Choose folders from storage. The launcher keeps read access on this device; it does not upload or redistribute your files.",
                14, 0xffd2dce4), margins(matchWrap(), 0, 0, 0, dp(12)));

        fafStatus = addImportRow("FAF client folder", "faf_uri", PICK_FAF,
                "Choose the FAF client or data folder");
        scfaStatus = addImportRow("Supreme Commander: Forged Alliance", "scfa_uri", PICK_SCFA,
                "Choose the SCFA installation folder");

        Button start = button("Start FAF");
        start.setEnabled(false);
        content.addView(start, margins(matchWrap(), 0, dp(8), 0, dp(4)));
        content.addView(text("Game launch will be enabled when the Android game runtime is ported.",
                13, 0xffffcc80), margins(matchWrap(), 0, 0, 0, dp(20)));

        addHeading("Settings");
        content.addView(text("Saved locally for the Android runtime. Game rendering and audio are not active yet.",
                14, 0xffd2dce4), margins(matchWrap(), 0, 0, 0, dp(10)));

        content.addView(text("Graphics backend", 14, 0xffa9bac8), matchWrap());
        renderer = new Spinner(this);
        ArrayAdapter<String> backendAdapter = new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_item, new String[]{"Vulkan", "OpenGL ES"});
        backendAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        renderer.setAdapter(backendAdapter);
        renderer.setSelection(preferences.getString("renderer", "Vulkan").equals("OpenGL ES") ? 1 : 0);
        content.addView(renderer, margins(matchWrap(), 0, 0, 0, dp(10)));

        resolution = addInput("Resolution", "1280x720", "resolution");
        frameLimit = addInput("Frame limit", "60", "frame_limit");
        vsync = new CheckBox(this);
        vsync.setText("Vertical sync");
        vsync.setTextColor(0xffedf3f7);
        vsync.setChecked(preferences.getBoolean("vsync", true));
        content.addView(vsync, matchWrap());
        logsEnabled = new CheckBox(this);
        logsEnabled.setText("Write launcher logs");
        logsEnabled.setTextColor(0xffedf3f7);
        logsEnabled.setChecked(preferences.getBoolean("logs_enabled", true));
        content.addView(logsEnabled, margins(matchWrap(), 0, 0, 0, dp(8)));

        Button save = button("Save settings");
        save.setOnClickListener(v -> saveSettings());
        content.addView(save, margins(matchWrap(), 0, 0, 0, dp(20)));

        addHeading("Logs");
        content.addView(text("Launcher import and settings events are recorded here. Game logs will appear after runtime support is added.",
                14, 0xffd2dce4), margins(matchWrap(), 0, 0, 0, dp(10)));
        Button showLogs = button("View logs");
        showLogs.setOnClickListener(v -> showLogs());
        content.addView(showLogs, margins(matchWrap(), 0, 0, 0, dp(8)));
        Button clearLogs = button("Clear logs");
        clearLogs.setOnClickListener(v -> clearLogs());
        content.addView(clearLogs, matchWrap());

        refreshImportStatus(fafStatus, "faf_uri", "FAF client folder");
        refreshImportStatus(scfaStatus, "scfa_uri", "SCFA game folder");
        setContentView(scroll);
    }

    private TextView addImportRow(String title, String preferenceKey, int requestCode, String pickerTitle) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.VERTICAL);
        row.setPadding(dp(12), dp(10), dp(12), dp(10));
        row.setBackgroundColor(0xff1a2934);
        TextView heading = text(title, 16, 0xffffffff);
        row.addView(heading, matchWrap());
        TextView status = text("No folder linked", 13, 0xffa9bac8);
        row.addView(status, margins(matchWrap(), 0, dp(3), 0, dp(7)));
        LinearLayout buttons = new LinearLayout(this);
        Button choose = button("Choose folder");
        choose.setOnClickListener(v -> chooseFolder(requestCode, pickerTitle));
        buttons.addView(choose, new LinearLayout.LayoutParams(0, dp(46), 1));
        Button remove = button("Remove");
        remove.setOnClickListener(v -> removeFolder(preferenceKey, title, status));
        LinearLayout.LayoutParams removeParams = new LinearLayout.LayoutParams(0, dp(46), 1);
        removeParams.leftMargin = dp(8);
        buttons.addView(remove, removeParams);
        row.addView(buttons, matchWrap());
        content.addView(row, margins(matchWrap(), 0, 0, 0, dp(10)));
        return status;
    }

    private EditText addInput(String label, String fallback, String key) {
        content.addView(text(label, 14, 0xffa9bac8), margins(matchWrap(), 0, 0, 0, dp(4)));
        EditText field = new EditText(this);
        field.setSingleLine(true);
        field.setTextColor(0xffffffff);
        field.setHintTextColor(0xff8295a4);
        field.setHint(fallback);
        field.setText(preferences.getString(key, fallback));
        field.setPadding(dp(10), 0, dp(10), 0);
        content.addView(field, margins(matchWrap(), 0, 0, 0, dp(8)));
        if (key.equals("resolution")) resolution = field;
        if (key.equals("frame_limit")) frameLimit = field;
        return field;
    }

    private void chooseFolder(int requestCode, String title) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        intent.putExtra("android.content.extra.SHOW_ADVANCED", true);
        try {
            startActivityForResult(Intent.createChooser(intent, title), requestCode);
        } catch (Exception e) {
            appendLog("Could not open folder picker: " + e.getMessage());
            toast("Android file picker is unavailable");
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null || data.getData() == null) return;

        String key;
        String label;
        TextView status;
        if (requestCode == PICK_FAF) {
            key = "faf_uri";
            label = "FAF client folder";
            status = fafStatus;
        } else if (requestCode == PICK_SCFA) {
            key = "scfa_uri";
            label = "SCFA game folder";
            status = scfaStatus;
        } else {
            return;
        }

        Uri uri = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            preferences.edit().putString(key, uri.toString()).apply();
            status.setText(summarizeFolder(uri));
            appendLog(label + " linked: " + status.getText());
        } catch (Exception e) {
            status.setText("Folder access could not be saved. Choose it again.");
            appendLog("Failed to link " + label + ": " + e.getMessage());
        }
    }

    private String summarizeFolder(Uri tree) {
        Cursor cursor = null;
        try {
            String documentId = DocumentsContract.getTreeDocumentId(tree);
            Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, documentId);
            cursor = getContentResolver().query(children,
                    new String[]{DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                            DocumentsContract.Document.COLUMN_MIME_TYPE}, null, null, null);
            if (cursor == null) return "Folder linked; contents could not be listed.";
            int count = cursor.getCount();
            ArrayList<String> names = new ArrayList<>();
            int nameColumn = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_DISPLAY_NAME);
            while (cursor.moveToNext() && names.size() < 5) {
                if (nameColumn >= 0) names.add(cursor.getString(nameColumn));
            }
            String summary = "Folder linked · " + count + " top-level entries";
            if (!names.isEmpty()) summary += "\n" + joinNames(names);
            return summary;
        } catch (Exception e) {
            return "Folder linked; file listing unavailable.";
        } finally {
            if (cursor != null) cursor.close();
        }
    }

    private String joinNames(ArrayList<String> names) {
        StringBuilder result = new StringBuilder();
        for (String name : names) {
            if (result.length() > 0) result.append(" · ");
            result.append(name);
        }
        return result.toString();
    }

    private void refreshImportStatus(TextView status, String key, String label) {
        String value = preferences.getString(key, null);
        if (value == null) {
            status.setText("No folder linked");
            return;
        }
        status.setText(summarizeFolder(Uri.parse(value)));
    }

    private void removeFolder(String key, String label, TextView status) {
        String value = preferences.getString(key, null);
        if (value != null) {
            try {
                getContentResolver().releasePersistableUriPermission(Uri.parse(value),
                        Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (Exception ignored) {
                // The saved link is still removed from launcher settings.
            }
        }
        preferences.edit().remove(key).apply();
        status.setText("No folder linked");
        appendLog(label + " link removed");
    }

    private void saveSettings() {
        String newResolution = resolution.getText().toString().trim();
        String newFrameLimit = frameLimit.getText().toString().trim();
        if (!newResolution.matches("[0-9]{3,5}x[0-9]{3,5}")) {
            toast("Resolution must look like 1280x720");
            return;
        }
        try {
            int fps = Integer.parseInt(newFrameLimit);
            if (fps < 15 || fps > 240) throw new NumberFormatException();
        } catch (NumberFormatException e) {
            toast("Frame limit must be between 15 and 240");
            return;
        }
        preferences.edit()
                .putString("renderer", renderer.getSelectedItem().toString())
                .putString("resolution", newResolution)
                .putString("frame_limit", newFrameLimit)
                .putBoolean("vsync", vsync.isChecked())
                .putBoolean("logs_enabled", logsEnabled.isChecked())
                .apply();
        appendLog("Settings saved: " + renderer.getSelectedItem() + ", " + newResolution
                + ", " + newFrameLimit + " FPS");
        toast("Settings saved on this device");
    }

    private void showLogs() {
        String value = readLogs();
        TextView body = text(value.isEmpty() ? "No launcher log entries." : value, 13, 0xffe3ebf1);
        body.setTypeface(android.graphics.Typeface.MONOSPACE);
        body.setTextIsSelectable(true);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(body);
        new AlertDialog.Builder(this).setTitle("Launcher logs").setView(scroll)
                .setPositiveButton("Close", null).show();
    }

    private void clearLogs() {
        File file = logFile();
        if (file.exists() && !file.delete()) {
            toast("Could not clear logs");
            return;
        }
        toast("Logs cleared");
    }

    private void appendLog(String message) {
        if (preferences != null && !preferences.getBoolean("logs_enabled", true)) return;
        File directory = new File(getFilesDir(), "logs");
        if (!directory.exists() && !directory.mkdirs()) return;
        String stamp = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(new Date());
        try (FileOutputStream output = new FileOutputStream(new File(directory, "launcher.log"), true)) {
            output.write((stamp + "  " + message + "\n").getBytes("UTF-8"));
        } catch (IOException ignored) {
            // Logging must not block launcher use.
        }
    }

    private String readLogs() {
        File file = logFile();
        if (!file.exists()) return "";
        try (RandomAccessFile input = new RandomAccessFile(file, "r")) {
            long length = input.length();
            long start = Math.max(0, length - 16000);
            input.seek(start);
            byte[] bytes = new byte[(int) (length - start)];
            input.readFully(bytes);
            return new String(bytes, "UTF-8");
        } catch (IOException e) {
            return "Could not read log: " + e.getMessage();
        }
    }

    private File logFile() {
        return new File(new File(getFilesDir(), "logs"), "launcher.log");
    }

    private String getVersionName() {
        try {
            return getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
        } catch (Exception e) {
            return "development";
        }
    }

    private void addHeading(String value) {
        TextView heading = text(value, 19, 0xffffffff);
        content.addView(heading, margins(matchWrap(), 0, 0, 0, dp(7)));
    }

    private Button button(String value) {
        Button button = new Button(this);
        button.setText(value);
        return button;
    }

    private TextView text(String value, int size, int color) {
        TextView text = new TextView(this);
        text.setText(value);
        text.setTextSize(size);
        text.setTextColor(color);
        text.setPadding(0, dp(2), 0, dp(2));
        return text;
    }

    private LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private LinearLayout.LayoutParams margins(LinearLayout.LayoutParams params,
                                               int left, int top, int right, int bottom) {
        params.setMargins(left, top, right, bottom);
        return params;
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }

    private void toast(String value) {
        android.widget.Toast.makeText(this, value, android.widget.Toast.LENGTH_SHORT).show();
    }
}
