'use strict';
module.exports = function(grunt) {

  grunt.initConfig({
    jshint: {
      options: {
        jshintrc: '.jshintrc'
      },
      all: [
        'Gruntfile.js',
        'resources/js/pages/*.js'
      ]
    },
    less: {
      dist: {
        files: {
          'resources/css/main.min.css': [
            'resources/css/normalize.css',
            'resources/css/snippets.css',
            'resources/css/magnific-popup.css',
            'resources/css/main.less'
          ]
        },
        options: {
          compress: true,
          // LESS source map
          // To enable, set sourceMap to true and update sourceMapRootpath based on your install
          sourceMap: false,
          sourceMapFilename: 'resources/css/main.min.css.map',
          sourceMapRootpath: 'resources/css'
        }
      }
    },
    uglify: {
      dist: {
        files: {
          'resources/js/scripts.min.js': [
            'resources/js/plugins/*.js',
            'resources/js/pages/base.js',
            'resources/js/pages/*.js',
          ]
        },
        options: {
          // JS source map: to enable, uncomment the lines below and update sourceMappingURL based on your install
          // sourceMap: 'assets/js/scripts.min.js.map',
          // sourceMappingURL: '/app/themes/roots/assets/js/scripts.min.js.map'
        }
      }
    },
    markdown: {
      all: {
        files: [{
          expand: true,
          src: 'templates/pages/docs/src/markdown/*.md',
          dest: 'templates/pages/docs/src/html',
          flatten: true,
          ext: '.html'
        }],
        options: {
          template: 'templates/pages/docs/src/layout.jst',
          markdownOptions: {
          }
        }
      }
    },
    watch: {
      less: {
        files: [
          'resources/css/*.less',
          'resources/css/plugins/*.css'
        ],
        tasks: ['less']
      },
      md: {
        files: [
          'templates/pages/docs/src/markdown/*.md',
        ],
        tasks: ['markdown']
      },
      js: {
        files: [
          'resources/js/plugins/*.js',
          'resources/js/pages/*.js'
        ],
        tasks: ['jshint', 'uglify']
      },
      livereload: {
        // Browser live reloading
        // https://github.com/gruntjs/grunt-contrib-watch#live-reloading
        options: {
          livereload: false
        },
        files: [
          'resources/css/main.min.css',
          'resources/js/scripts.min.js',
          '*.php'
        ]
      }
    },
    clean: {
      dist: [
        'resources/css/main.min.css',
        'resources/js/scripts.min.js'
      ]
    }
  });

  // Load tasks
  grunt.loadNpmTasks('grunt-contrib-clean');
  grunt.loadNpmTasks('grunt-contrib-jshint');
  grunt.loadNpmTasks('grunt-contrib-uglify');
  grunt.loadNpmTasks('grunt-contrib-watch');
  grunt.loadNpmTasks('grunt-contrib-less');
  grunt.loadNpmTasks('grunt-markdown');

  // Register tasks
  grunt.registerTask('default', [
    'clean',
    'less',
    'uglify',
    'markdown'
  ]);
  grunt.registerTask('dev', [
    'watch'
  ]);

};
