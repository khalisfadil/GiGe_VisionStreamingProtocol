function closeVideo(video_writer, filename)
    global VIDEO_OUTPUT_DIR;
    if ~isempty(video_writer) && isvalid(video_writer)
        full_path = fullfile(VIDEO_OUTPUT_DIR, filename);
        fprintf('Closing video: %s\n', full_path);
        close(video_writer);
    end
end