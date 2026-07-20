// Returns a sharpness score. Higher value = clearer frame.
double calculate_frame_sharpness(const unsigned char *y_buffer, int width, int height) {
    if (!y_buffer || width < 3 || height < 3) return 0.0;

    long long total_pixels = (width - 2) * (height - 2);
    double sum = 0.0;
    double sq_sum = 0.0;

    // Allocate array for Laplacian response values
    double *laplacian_results = (double *)malloc(total_pixels * sizeof(double));
    if (!laplacian_results) return 0.0;

    int idx = 0;

    // Apply a 3x3 Laplacian Kernel:
    // [  0,  1,  0 ]
    // [  1, -4,  1 ]
    // [  0,  1,  0 ]
    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            int current_pixel = y * width + x;

            double laplacian = (double)y_buffer[current_pixel - width] +  // Top
                               (double)y_buffer[current_pixel - 1] +      // Left
                               (double)y_buffer[current_pixel + 1] +      // Right
                               (double)y_buffer[current_pixel + width] -  // Bottom
                               (4.0 * (double)y_buffer[current_pixel]);   // Center

            laplacian_results[idx++] = laplacian;
            sum += laplacian;
            sq_sum += (laplacian * laplacian);
        }
    }

    // Calculate Variance: Var = E[X^2] - (E[X])^2
    double mean = sum / total_pixels;
    double mean_sq = sq_sum / total_pixels;
    double variance = mean_sq - (mean * mean);

    free(laplacian_results);
    return variance;
}