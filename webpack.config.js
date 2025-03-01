const path = require('path');
const TerserPlugin = require('terser-webpack-plugin');

module.exports = {
  entry: './src/js/scheduler/index.js',
  output: {
    filename: 'scheduler-bundle.js',
    path: path.resolve(__dirname, 'data/js'),
  },
  module: {
    rules: [
      {
        test: /\.js$/,
        exclude: /node_modules/,
        use: {
          loader: 'babel-loader',
          options: {
            presets: ['@babel/preset-env']
          }
        }
      }
    ]
  },
  optimization: {
    minimize: true,
    minimizer: [new TerserPlugin({
      terserOptions: {
        format: {
          comments: false,
        },
      },
      extractComments: false,
    })],
    runtimeChunk: false,
    splitChunks: {
      cacheGroups: {
        default: false
      }
    }
  },
  performance: {
    hints: false
  }
};