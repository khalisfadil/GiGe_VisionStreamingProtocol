% Main script: parse_gvsp.m
clear all;
close all;

% Load configuration
load('gvsp_config.mat');

% List available GigE cameras
cameraList = gigecamlist;
if isempty(cameraList)
    error('No GigE Vision cameras detected. Ensure the camera is connected and the GigE Vision Hardware Support Package is installed.');
end
disp(cameraList);

% Connect to the camera using device index (1 for first camera)
try
    g = gigecam(1);
    fprintf('Connected to camera at %s\n', UDP_IP);
catch ME
    fprintf('Failed to connect to camera: %s\n', ME.message);
    return;
end

% Configure camera properties 
g.PixelFormat = 'BayerRG8'; % Set to 'RGB8' 
disp(g); % Display camera properties
commands(g); % Show available commands for reference

% Check if timestamp is supported
use_camera_timestamp = isprop(g, 'GevTimestampValue');
if use_camera_timestamp
    fprintf('Using camera timestamp (GevTimestampValue).\n');
else
    fprintf('Camera timestamp not available. Falling back to MATLAB ''now'' timestamp.\n');
end

% Initialize variables
g_start_timestamp = 0;
g_current_block_id = 0;
segment_start_time = tic;
video_writer = [];

try
    while true
        % Acquire a single image from the camera
        payload_image = snapshot(g); % Returns image array
        % Get start timestamp: camera if available, otherwise MATLAB's now
        if use_camera_timestamp
            % Camera timestamp in nanoseconds, convert to seconds for datetime
            % Note: Assuming camera epoch is UNIX (1970)
            g_start_timestamp = double(g.GevTimestampValue) / 1e9; % ns to seconds
            start_dt = datetime(g_start_timestamp, 'ConvertFrom', 'posixtime');
        else
            start_dt = datetime('now'); % MATLAB's current time
        end
        g_current_block_id = g_current_block_id + 1;

        % Detect format based on image dimensions
        [height, width, channels] = size(payload_image);
        is_rgb = (channels == 3);
        fprintf('Acquired image - Size: %dx%d, Channels: %d, RGB: %d, Timestamp: %s\n', ...
            width, height, channels, is_rgb, datestr(start_dt, 'dd-mmm-yyyy HH:MM:SS'));

        % Verify image size matches expected dimensions
        expected_size = IMAGE_WIDTH * IMAGE_HEIGHT * (3 * is_rgb + ~is_rgb);
        if numel(payload_image) ~= expected_size
            fprintf('Image size mismatch: %d vs expected %d (RGB: %d)\n', ...
                numel(payload_image), expected_size, is_rgb);
            continue;
        end

        % Initialize video writer if not already open
        if isempty(video_writer) || ~isvalid(video_writer)
            if use_camera_timestamp
                end_timestamp = double(g.GevTimestampValue) / 1e9; % ns to seconds
                end_dt = datetime(end_timestamp, 'ConvertFrom', 'posixtime');
            else
                end_dt = datetime('now');
            end
            filename = sprintf('%03d_%s_%s.avi', g_current_block_id, ...
                datestr(start_dt, 'yyyymmdd_HHMMSS'), datestr(end_dt, 'yyyymmdd_HHMMSS'));
            full_path = fullfile(VIDEO_OUTPUT_DIR, filename);
            if ~exist(VIDEO_OUTPUT_DIR, 'dir')
                mkdir(VIDEO_OUTPUT_DIR);
            end
            video_writer = VideoWriter(full_path, 'Motion JPEG AVI');
            video_writer.FrameRate = 10;
            open(video_writer);
        end

        % Show and write image with timestamp
        showImage(payload_image, video_writer, start_dt, is_rgb);

        % Check video duration
        elapsed_sec = toc(segment_start_time);
        if elapsed_sec >= VIDEO_DURATION_SEC
            if use_camera_timestamp
                end_timestamp = double(g.GevTimestampValue) / 1e9; % ns to seconds
                end_dt = datetime(end_timestamp, 'ConvertFrom', 'posixtime');
            else
                end_dt = datetime('now');
            end
            filename = sprintf('%03d_%s_%s.avi', g_current_block_id, ...
                datestr(start_dt, 'yyyymmdd_HHMMSS'), datestr(end_dt, 'yyyymmdd_HHMMSS'));
            closeVideo(video_writer, filename);
            segment_start_time = tic;
        end

        % Non-blocking quit check
        drawnow; % Update figure to check for keypress
        if ~isempty(get(gcf, 'CurrentCharacter')) && get(gcf, 'CurrentCharacter') == 'q'
            if use_camera_timestamp
                end_timestamp = double(g.GevTimestampValue) / 1e9; % ns to seconds
                end_dt = datetime(end_timestamp, 'ConvertFrom', 'posixtime');
            else
                end_dt = datetime('now');
            end
            filename = sprintf('%03d_%s_%s.avi', g_current_block_id, ...
                datestr(start_dt, 'yyyymmdd_HHMMSS'), datestr(end_dt, 'yyyymmdd_HHMMSS'));
            closeVideo(video_writer, filename);
            break;
        end
    end
catch ME
    fprintf('Error occurred: %s\n', ME.message);
    if ~isempty(video_writer) && isvalid(video_writer)
        close(video_writer);
    end
    delete(g); % Clean up gigecam object
    rethrow(ME);
end

% Cleanup
if ~isempty(video_writer) && isvalid(video_writer)
    close(video_writer);
end
delete(g); % Disconnect from camera