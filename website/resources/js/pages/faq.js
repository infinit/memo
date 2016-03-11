$(document).ready(function() {

  function getTemplate(templateName) {
    return document.querySelector('#' + templateName + '-template').innerHTML;
  }

  if ($('body').hasClass('faq')) {

    var search = instantsearch({
      appId: '2BTFITEL0N',
      apiKey: 'f27772a74737b4bb66eb0999104830d2',
      indexName: 'infinit_sh_faq',
      urlSync: true
    });

    instantsearch.widgets.singleFacet = function singleFacet(options) {
      options.template = Hogan.compile(options.template);
      var $container = $(options.container);

      return {
        getConfiguration: function(/*currentSearchParams*/) {
          // Make sure the facet used for this widget is declared
          return {
            facets: [options.facet.attributeName]
          };
        },
        init: function(params) {
          $container.on('click', '.facet', function(e) {
            e.preventDefault();

            params.helper.removeFacetRefinement(options.facet.attributeName);

            if (options.facet.value !== 'popular') {
              params.helper.addFacetRefinement(options.facet.attributeName, options.facet.value);
            }

            params.helper.search();
          });
        },
        render: function(params) {
          // We know we only activate one refinement per facet, so just get the first one
          var refinement = params.helper.getRefinements(options.facet.attributeName)[0];

          if (!refinement && options.facet.value === 'popular') {
            active = true;
          } else {
            active = refinement ? refinement.value === options.facet.value : false;
          }

          $container.html(options.template.render({
            label: options.facet.label,
            active: active,
            icon_name: options.facet.icon_name
          }));

        }
      };
    };

    var widgets = [
      instantsearch.widgets.searchBox({
        container: '#search-input',
        placeholder: 'Search a question...'
      }),

      instantsearch.widgets.hits({
        container: '#hits',
        hitsPerPage: 20,
        templates: {
          item: getTemplate('hit'),
          empty: getTemplate('no-results')
        }
      }),

      instantsearch.widgets.singleFacet({
        container: $('.facets .popular'),
        template: $('#single-facet-template').html(),
        facet: {
          attributeName: 'category',
          value: 'popular',
          label: 'Popular',
          icon_name: 'fire'
        }
      }),

      instantsearch.widgets.singleFacet({
        container: $('.facets .general'),
        template: $('#single-facet-template').html(),
        facet: {
          attributeName: 'category',
          value: 'general',
          label: 'General',
          icon_name: 'idea-clean'
        }
      }),

      instantsearch.widgets.singleFacet({
        container: $('.facets .technology'),
        template: $('#single-facet-template').html(),
        facet: {
          attributeName: 'category',
          value: 'technology',
          label: 'Technology',
          icon_name: 'code'
        }
      }),

      instantsearch.widgets.singleFacet({
        container: $('.facets .comparisons'),
        template: $('#single-facet-template').html(),
        facet: {
          attributeName: 'category',
          value: 'comparisons',
          label: 'Comparisons',
          icon_name: 'comparison'
        }
      }),

      instantsearch.widgets.singleFacet({
        container: $('.facets .security'),
        template: $('#single-facet-template').html(),
        facet: {
          attributeName: 'category',
          value: 'security',
          label: 'Security',
          icon_name: 'lock'
        }
      })
    ];

    widgets.forEach(search.addWidget, search);

    search.start();
  }

});