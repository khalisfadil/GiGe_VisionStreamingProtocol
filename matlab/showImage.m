function showImage(payload, video_writer, timestamp, is_rgb)
    image = payload;

    %if isdatetime(timestamp)
    %    timestamp_str = sprintf('Timestamp: %.0f ms', timestamp / 1e3);
    %else
    %    fprintf("invalid Timestamp: %s. using defualt./n",class(timestamp));
    %    timestamp_str="Timestamp: Unknown";
    %end

    timestamp_str = datestr(timestamp, 'dd-mmm-yyyy HH:MM:SS');
    
    imshow(image);
    hold on;
    text(10, 30, timestamp_str, 'Color', 'white', 'FontSize', 10, 'BackgroundColor', 'black');
    hold off;
    drawnow;

    f = getframe(gcf);
    if ~isempty(video_writer) && isvalid(video_writer)
        writeVideo(video_writer, f);
    end
end